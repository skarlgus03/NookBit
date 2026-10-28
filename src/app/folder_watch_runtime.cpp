#include "app/folder_watch_runtime.hpp"
#include <QDir>
#include <utility>

namespace nookbit {
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
