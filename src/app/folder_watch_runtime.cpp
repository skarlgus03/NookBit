#include "app/folder_watch_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include <QCoreApplication>
#include <QDir>
#include <utility>

namespace nookbit {
// 공통 설정 흐름에는 컨트롤러 대신 화면 경로와 명시적인 입력만 등록하는 함수.
Result<void> FolderWatchRuntime::registerUi(UiPageRegistry& registry) {
    return registry.addPage({"watch.settings", "folder-watch", 3,
        QCoreApplication::translate("WatchUi", "폴더 감시 설정"), "folder", {}, 0,
        QUrl(QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/FolderWatchSettingsPage.qml")),
        {{"controller", QVariant::fromValue(static_cast<QObject*>(&controller_))}}, false, UiPageHost::SettingsDetail}, uiLifetime_);
}
// 요약 문구와 입력 동작의 구성은 감시 기능이 소유하도록 제공하는 함수.
QVariantMap FolderWatchRuntime::settingsEntryUi() {
    return {{"source", QUrl(QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/FolderWatchSettingsEntry.qml"))},
        {"properties", QVariantMap{{"controller", QVariant::fromValue(static_cast<QObject*>(&controller_))}}}};
}
// 제안 수락·미루기 동작은 기능 소유 화면이 연결하도록 제공하는 함수.
QVariantMap FolderWatchRuntime::proposalUi() {
    return {{"source", QUrl(QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/FolderWatchProposal.qml"))},
        {"properties", QVariantMap{{"controller", QVariant::fromValue(static_cast<QObject*>(&controller_))}}}};
}
// 내부 의존성을 먼저 생성하고 컨트롤러를 마지막에 연결하는 생성자.
FolderWatchRuntime::FolderWatchRuntime(FolderWatchRuntimeConfig config, IClassificationQuery& classification,
    IPendingPathsQuery& pendingPaths, IEnqueuePendingPaths& pendingCommands, QSettings& preferences)
    : preferences_(preferences), persistSnooze_(config.persistSnooze),
      settings_(std::move(config.settingsPath), std::move(config.schemaPath)),
      evaluate_(scanner_, classification),
      controller_(pendingPaths, pendingCommands, settings_, evaluate_, std::move(config.defaultFolder)) {
    QObject::connect(&controller_, &WatchController::snoozeDeadlineChanged, &controller_, [this](qint64 deadline) {
        // 컨트롤러 수명 안에서만 제안 대기 시각을 저장하는 함수.
        saveSnooze(deadline);
    });
    restoreSnooze();
}

// 검증 모드를 제외하고 원래의 폴더·시각 설정 키를 유지하는 함수.
void FolderWatchRuntime::saveSnooze(qint64 deadline) {
    if (!persistSnooze_) return;
    preferences_.setValue(QStringLiteral("watchSnoozeFolder"), controller_.folder());
    preferences_.setValue(QStringLiteral("watchSnoozeUntilUtcMs"), deadline);
    preferences_.sync();
}

// 다른 폴더의 대기 시각을 적용하지 않도록 현재 폴더와 대조하는 함수.
void FolderWatchRuntime::restoreSnooze() {
    if (!persistSnooze_) return;
    if (QString::compare(QDir::cleanPath(preferences_.value(QStringLiteral("watchSnoozeFolder")).toString()),
        QDir::cleanPath(controller_.folder()), Qt::CaseInsensitive) == 0)
        controller_.restoreSnoozeUntil(preferences_.value(QStringLiteral("watchSnoozeUntilUtcMs"), 0).toLongLong());
}
}
