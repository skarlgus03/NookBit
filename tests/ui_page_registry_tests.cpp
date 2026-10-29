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
}
// 실제 화면 없이 등록 유효성·독립 수명·조회 계약을 검사하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    testRegistration();
    testLifetime();
    if (!failures) std::cout << "All UI-page registration tests passed.\n";
    return failures ? 1 : 0;
}
