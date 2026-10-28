#pragma once
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "presentation/watch_controller.hpp"
#include <QSettings>

namespace nookbit {
struct FolderWatchRuntimeConfig {
    QString settingsPath;
    QString schemaPath;
    QString defaultFolder;
    bool persistSnooze{true};
};

/// 감시 설정·검사 일꾼·제안 유스케이스·컨트롤러를 의존 순서대로 소유하는 조립 묶음.
/// 분류·대기 목록·사용자 설정 창구는 외부 소유이며 이 묶음보다 오래 살아 있어야 한다.
class FolderWatchRuntime final {
public:
    /// 감시 기능 내부 객체를 만들고 기존 제안 대기 설정을 복원하는 생성자.
    FolderWatchRuntime(FolderWatchRuntimeConfig config, IClassificationQuery& classification,
        IPendingPathsQuery& pendingPaths, IEnqueuePendingPaths& pendingCommands, QSettings& preferences);
    /// 컨트롤러와 타이머를 해제한 뒤 그들이 사용하는 일꾼을 해제하는 소멸자.
    ~FolderWatchRuntime() = default;
    /// 이 묶음이 살아 있는 동안만 유효한 감시 화면·입력 창구를 반환하는 함수.
    WatchController& controller() { return controller_; }
private:
    /// 현재 감시 폴더의 제안 대기 시각만 기존 설정 키에 저장하는 함수.
    void saveSnooze(qint64 deadline);
    /// 같은 감시 폴더에 저장된 제안 대기 시각만 복원하는 함수.
    void restoreSnooze();

    QSettings& preferences_;
    bool persistSnooze_;
    JsonWatchSettingsRepository settings_;
    FilesystemScanner scanner_;
    EvaluateWatchFolderUseCase evaluate_;
    WatchController controller_;
};
}
