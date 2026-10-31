#include "app/ui_page_registry.hpp"
#include <QThread>
#include <algorithm>

namespace nookbit {
// 호스트가 아직 직접 제공하는 페이지 번호를 예약하는 생성자.
UiPageRegistry::UiPageRegistry(QSet<int> reservedPages, QObject* parent)
    : QObject(parent), reservedPages_(std::move(reservedPages)) {}

// 텍스트 안내만 허용하고 잘못된 묶음이나 중복 이름은 부분 등록 없이 거부하는 함수.
Result<void> UiPageRegistry::addTutorialSteps(QList<UiTutorialStep> steps, QObject& lifetime) {
    if (QThread::currentThread() != thread() || lifetime.thread() != thread())
        return Result<void>::failure({"ui.tutorial_thread", "Tutorial registration requires the UI owning thread."});
    QSet<QString> ids;
    for (const auto& step : steps) {
        if (step.id.trimmed().isEmpty() || step.ownerId.trimmed().isEmpty() || step.title.trimmed().isEmpty()
            || step.summary.trimmed().isEmpty() || step.instructions.isEmpty()
            || std::any_of(step.instructions.cbegin(), step.instructions.cend(), [](const QString& text) {
                // 비어 있는 설명 항목을 등록 전에 확인하는 함수.
                return text.trimmed().isEmpty();
            }))
            return Result<void>::failure({"ui.invalid_tutorial", "Tutorial steps need stable names, a title, a summary and instructions."});
        if (tutorialSteps_.contains(step.id) || ids.contains(step.id))
            return Result<void>::failure({"ui.duplicate_tutorial", "Tutorial step name is already registered."});
        ids.insert(step.id);
    }
    if (steps.isEmpty()) return Result<void>::success();
    for (auto& step : steps) { const auto id = step.id; tutorialSteps_.insert(id, std::move(step)); }
    QObject::connect(&lifetime, &QObject::destroyed, this, [this, ids] {
        // 모듈이 사라지면 해당 모듈이 소유한 안내만 제거하는 함수.
        for (const auto& id : ids) tutorialSteps_.remove(id);
        emit tutorialStepsChanged();
    });
    emit tutorialStepsChanged();
    return Result<void>::success();
}

// 안내에 실행 창구를 포함하지 않고 QML에 순수 값 복사본만 전달하는 함수.
QVariantList UiPageRegistry::tutorialSteps() const {
    auto sorted = tutorialSteps_.values();
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        // 같은 순서 값에서도 등록 시점에 따라 안내가 섞이지 않게 비교하는 함수.
        return left.order != right.order ? left.order < right.order : left.id < right.id;
    });
    QVariantList result;
    for (const auto& step : sorted)
        result.append(QVariantMap{{"id", step.id}, {"ownerId", step.ownerId}, {"title", step.title},
            {"summary", step.summary}, {"instructions", step.instructions}, {"note", step.note}});
    return result;
}

// 중복·예약 번호·외부 QML 경로를 거부하고 소유 수명에 등록을 연결하는 함수.
Result<void> UiPageRegistry::addPage(UiPageContribution contribution, QObject& lifetime) {
    return addPages({std::move(contribution)}, lifetime);
}

// 일부 페이지가 잘못돼도 나머지만 남지 않도록 전체 검증 후 등록하는 함수.
Result<void> UiPageRegistry::addPages(QList<UiPageContribution> contributions, QObject& lifetime) {
    if (QThread::currentThread() != thread() || lifetime.thread() != thread())
        return Result<void>::failure({"ui.page_thread", "Page registration requires the UI owning thread."});
    QSet<int> routes;
    QSet<QString> names;
    for (const auto& contribution : contributions) {
        if (contribution.id.trimmed().isEmpty() || contribution.ownerId.trimmed().isEmpty() ||
            contribution.page <= 0 || contribution.title.trimmed().isEmpty() || contribution.icon.trimmed().isEmpty() ||
            !contribution.source.isValid() || contribution.source.scheme() != QStringLiteral("qrc") ||
            !contribution.source.path().endsWith(QStringLiteral(".qml")) || contribution.source.hasQuery() ||
            contribution.source.hasFragment() || !contribution.source.host().isEmpty())
            return Result<void>::failure({"ui.invalid_page", "A page needs stable names, a route, menu metadata and a bundled QML source."});
        if (contribution.host != UiPageHost::Extension && contribution.host != UiPageHost::SettingsDetail)
            return Result<void>::failure({"ui.invalid_page_host", "Page host must be an extension or settings-detail surface."});
        if (reservedPages_.contains(contribution.page) || pages_.contains(contribution.page) || pageId(contribution.id) != 0 ||
            routes.contains(contribution.page) || names.contains(contribution.id))
            return Result<void>::failure({"ui.duplicate_page", "Page name or route is already registered or reserved by the host."});
        routes.insert(contribution.page);
        names.insert(contribution.id);
    }
    if (contributions.isEmpty()) return Result<void>::success();
    for (auto& contribution : contributions) {
        const int route = contribution.page;
        pages_.insert(route, std::move(contribution));
    }
    QObject::connect(&lifetime, &QObject::destroyed, this, [this, routes] {
        // 기능의 문맥이 사라지면 메뉴와 화면 입력을 함께 제거하는 함수.
        for (const int route : routes) pages_.remove(route);
        emit pagesChanged();
    });
    emit pagesChanged();
    return Result<void>::success();
}

// 등록 순서와 무관하게 메뉴 순서와 안정적인 이름으로 정렬하는 함수.
QVariantList UiPageRegistry::pages() const {
    auto sorted = pages_.values();
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        // 같은 순서 값에서도 메뉴 순서가 흔들리지 않게 비교하는 함수.
        return left.order != right.order ? left.order < right.order : left.id < right.id;
    });
    QVariantList result;
    for (const auto& entry : sorted) result.append(page(entry.page));
    return result;
}

// 등록 정보의 존재만 확인하고 화면을 생성하지 않는 함수.
bool UiPageRegistry::contains(int route) const { return pages_.contains(route); }
// 설정 상세로 표시할 페이지가 일반 호스트에도 만들어지는 것을 방지하는 함수.
QVariantList UiPageRegistry::extensionPages() const {
    QVariantList result;
    for (const auto& value : pages())
        if (value.toMap().value("host").toString() == QStringLiteral("extension")) result.append(value);
    return result;
}

// QML이 전역 기능 이름을 찾지 않고 전달된 입력만 사용하도록 반환하는 함수.
QVariantMap UiPageRegistry::page(int route) const {
    const auto found = pages_.constFind(route);
    if (found == pages_.cend()) return {};
    const auto& entry = found.value();
    return {{"id", entry.id}, {"ownerId", entry.ownerId}, {"page", entry.page}, {"title", entry.title},
        {"label", entry.title}, {"icon", entry.icon}, {"section", entry.section}, {"order", entry.order},
        {"source", entry.source}, {"properties", entry.properties}, {"inMenu", entry.inMenu}, {"homeShortcut", entry.homeShortcut},
        {"host", entry.host == UiPageHost::SettingsDetail ? QStringLiteral("settingsDetail") : QStringLiteral("extension")}};
}

// 등록되지 않은 이름에는 홈을 뜻하는 0을 반환하는 함수.
int UiPageRegistry::pageId(const QString& id) const {
    for (const auto& entry : pages_) if (entry.id == id) return entry.page;
    return 0;
}
}
