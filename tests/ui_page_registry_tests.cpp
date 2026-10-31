#include "app/ui_page_registry.hpp"
#include <QCoreApplication>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
int failures{};
// 등록 계약의 위반이나 상태 변경을 설명과 함께 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 화면을 생성하지 않는 작은 등록 문서를 만드는 함수.
nookbit::UiPageContribution entry(QString id, int route, int order = 40) {
    return {std::move(id), "companion", route, "Companion", "companion", "Companion", order,
        QUrl("qrc:/test/Page.qml"), {}};
}
// 중복·호스트 번호·잘못된 입력이 기존 등록을 바꾸지 않는지 검사하는 함수.
void testRegistration() {
    nookbit::UiPageRegistry registry({1, 7, 8});
    QObject owner;
    int changes{};
    QObject::connect(&registry, &nookbit::UiPageRegistry::pagesChanged, &owner, [&] { ++changes; });
    expect(!registry.contains(5) && registry.pageId("companion.settings") == 0 && registry.page(5).isEmpty(), "empty registry has no page");
    expect(registry.addPage(entry("companion.settings", 5), owner).hasValue(), "register one page");
    expect(!registry.addPage(entry("duplicate.route", 5), owner), "duplicate route rejected");
    expect(!registry.addPage(entry("companion.settings", 12), owner), "duplicate stable name rejected");
    expect(!registry.addPage(entry("reserved", 7), owner), "host route reserved");
    expect(!registry.addPage(entry("home", 0), owner), "home route reserved");
    expect(!registry.addPage(entry("  ", 12), owner), "blank stable name rejected");
    auto external = entry("external", 12);
    external.source = QUrl("https://example.com/Page.qml");
    expect(!registry.addPage(external, owner), "external executable QML source rejected");
    auto badSource = entry("bad-source", 12);
    badSource.source = QUrl("qrc:/test/image.png");
    expect(!registry.addPage(badSource, owner), "non-QML source rejected");
    expect(changes == 1 && registry.pages().size() == 1 && registry.pageId("companion.settings") == 5,
        "failed additions leave existing pages unchanged and emit no updates");
    expect(registry.addPage(entry("before", 12, 20), owner).hasValue() && registry.pages().first().toMap().value("page").toInt() == 12,
        "menu ordering is independent of registration order");
    auto copy = registry.page(5);
    copy["title"] = "Modified copy";
    expect(registry.page(5).value("title").toString() == "Companion", "caller cannot mutate stored metadata through a snapshot");
    QObject foreign;
    QThread thread;
    foreign.moveToThread(&thread);
    expect(!registry.addPage(entry("foreign", 13), foreign), "foreign-thread lifetime context rejected");
}
// 각 등록은 기능의 수명 문맥이 끝날 때만 제거되고 다른 기능은 보존되는지 검사하는 함수.
void testLifetime() {
    nookbit::UiPageRegistry registry;
    QObject other;
    expect(registry.addPage(entry("other", 12), other).hasValue(), "other feature fixture");
    auto owner = std::make_unique<QObject>();
    expect(registry.addPage(entry("companion.settings", 5), *owner).hasValue(), "scoped feature fixture");
    owner.reset();
    expect(!registry.contains(5) && registry.pageId("companion.settings") == 0 && registry.contains(12),
        "owner destruction removes its page without affecting another feature");
    auto replacement = std::make_unique<QObject>();
    expect(registry.addPage(entry("companion.settings", 5), *replacement).hasValue(), "recreated feature can reuse its stable route");
    auto temporaryRegistry = std::make_unique<nookbit::UiPageRegistry>();
    expect(temporaryRegistry->addPage(entry("short-host", 15), *replacement).hasValue(), "short host fixture");
    temporaryRegistry.reset();
    replacement.reset();
    expect(registry.pages().size() == 1, "destroyed host does not leave callbacks on a longer-lived owner");
}
// 한 기능의 여러 페이지는 부분 등록 없이 한 번에 추가·제거되는지 검사하는 함수.
void testBatchRegistration() {
    nookbit::UiPageRegistry registry({8});
    auto owner = std::make_unique<QObject>();
    int changes{};
    QObject observer;
    QObject::connect(&registry, &nookbit::UiPageRegistry::pagesChanged, &observer, [&] { ++changes; });
    expect(!registry.addPages({entry("first", 1), entry("reserved", 8)}, *owner)
        && registry.pages().isEmpty() && changes == 0, "late batch conflict leaves no partial registration");
    expect(!registry.addPages({entry("first", 1), entry("same-route", 1)}, *owner)
        && registry.pages().isEmpty(), "duplicates within batch rejected atomically");
    expect(!registry.addPages({entry("same-name", 1), entry("same-name", 4)}, *owner)
        && registry.pages().isEmpty(), "duplicate batch stable names rejected atomically");
    expect(registry.addPages({entry("sort", 7, 10), entry("rules", 1, 20), entry("history", 4, 30)}, *owner).hasValue()
        && registry.pages().size() == 3 && changes == 1, "valid feature batch emits one update");
    auto hidden = entry("recovery", 10, 31);
    hidden.inMenu = false;
    expect(registry.addPage(hidden, *owner).hasValue() && registry.contains(10)
        && !registry.page(10).value("inMenu").toBool(), "auxiliary page remains routable without requesting a home menu");
    expect(!registry.page(7).value("homeShortcut").toBool(), "new module pages default to catalog-only instead of growing the home menu");
    owner.reset();
    expect(registry.pages().isEmpty() && changes == 4, "feature lifetime removes primary and auxiliary registrations");
}
// 설정 상세 페이지는 같은 경로를 유지하면서 일반 확장 화면에 중복 생성되지 않는지 검사하는 함수.
void testSettingsHost() {
    nookbit::UiPageRegistry registry;
    QObject owner;
    auto settings = entry("watch.settings", 3);
    settings.inMenu = false;
    settings.host = nookbit::UiPageHost::SettingsDetail;
    expect(registry.addPages({entry("notifications.settings", 11), settings}, owner).hasValue(), "register two different page hosts");
    expect(registry.pages().size() == 2 && registry.extensionPages().size() == 1
        && registry.extensionPages().first().toMap().value("page").toInt() == 11
        && registry.page(3).value("host").toString() == "settingsDetail", "settings detail route has exactly one intended render host");
    auto invalid = entry("invalid-host", 12);
    invalid.host = static_cast<nookbit::UiPageHost>(99);
    expect(!registry.addPage(invalid, owner) && registry.pages().size() == 2, "unknown render host does not add an unreachable page");
}
// 안내는 여러 장을 제공할 수 있고 순수 값·정렬·모듈 수명·묶음 검증을 지키는지 검사하는 함수.
void testTutorials() {
    nookbit::UiPageRegistry registry;
    auto owner = std::make_unique<QObject>();
    QObject other;
    nookbit::UiTutorialStep step{"organizer.first", "file-organizer", "First", "Summary", {"Instruction"}, "Note", 20};
    auto second = step; second.id = "organizer.second"; second.order = 10;
    expect(registry.addTutorialSteps({step, second}, *owner).hasValue(), "one module may contribute several tutorial pages");
    expect(registry.tutorialSteps().size() == 2 && registry.tutorialSteps().first().toMap().value("id") == second.id,
        "tutorials sorted independently of registration order");
    auto duplicate = step; duplicate.id = "new";
    expect(!registry.addTutorialSteps({duplicate, step}, *owner) && registry.tutorialSteps().size() == 2,
        "late duplicate leaves the entire tutorial batch unchanged");
    auto invalid = duplicate; invalid.instructions = {" "};
    expect(!registry.addTutorialSteps({invalid}, *owner), "blank tutorial instructions are rejected");
    invalid = duplicate; invalid.ownerId.clear();
    expect(!registry.addTutorialSteps({invalid}, *owner), "tutorials require a stable owner");
    QObject foreign; QThread thread; foreign.moveToThread(&thread);
    expect(!registry.addTutorialSteps({duplicate}, foreign), "foreign-thread tutorial registration rejected");
    duplicate.id = "other";
    expect(registry.addTutorialSteps({duplicate}, other).hasValue(), "independent tutorial fixture");
    auto copy = registry.tutorialSteps(); copy[0] = QVariantMap{{"title", "Changed"}};
    expect(registry.tutorialSteps().first().toMap().value("title") == "First", "tutorial snapshots cannot modify stored data");
    owner.reset();
    expect(registry.tutorialSteps().size() == 1 && registry.tutorialSteps().first().toMap().value("id") == "other",
        "module lifetime removes only its own tutorial pages");
}
}
// 실제 화면 없이 등록 유효성·독립 수명·조회 계약을 검사하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    testRegistration();
    testLifetime();
    testBatchRegistration();
    testSettingsHost();
    testTutorials();
    if (!failures) std::cout << "All UI-page registration tests passed.\n";
    return failures ? 1 : 0;
}
