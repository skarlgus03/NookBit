#pragma once
#include <nookbit/domain/result.hpp>
#include <QString>
#include <QVariantMap>
#include <QUrl>
#include <memory>

class QSettings;
namespace nookbit {
class PendingFilesModel;
class IPendingPathsQuery;
class IEnqueuePendingPaths;
class IClassificationQuery;
class BackgroundTaskRunner;
class PreviewController;
class HistoryController;
class RecoveryController;
class RuleEditorController;
class DestinationSettingsController;
class UiPageRegistry;

struct FileOrganizerRuntimeConfig {
    QString appData;
    QString defaultDestination;
    bool verifyUi{false};
};

/// 정리 기능의 저장소·유스케이스·화면 창구를 함께 생성하고 작업 종료 뒤 해제하는 묶음.
/// 외부 설정과 QML·기능 간 연결은 각각 이 묶음보다 오래, 짧게 살아 있어야 한다.
class FileOrganizerRuntime final {
public:
    /// 기존 경로와 검증 모드를 받아 정리 기능 내부 의존성을 구성하는 생성자.
    FileOrganizerRuntime(FileOrganizerRuntimeConfig config, QSettings& preferences);
    /// 진행 중인 작업이 끝날 때까지 의존성을 보존한 뒤 내부 객체를 해제하는 소멸자.
    ~FileOrganizerRuntime();
    /// 정리·규칙·기록·복구·충돌 페이지의 입력과 메뉴 표시 여부를 함께 등록하는 함수.
    Result<void> registerUi(UiPageRegistry& registry);
    /// 공통 설정 화면에 삽입할 기능 소유 영역과 빌려줄 입력을 반환하는 함수.
    QVariantMap settingsUi();
    /// 대기 목록의 화면 창구를 반환하는 함수.
    PendingFilesModel& pendingFiles();
    /// 다른 기능이 대기 경로의 값 복사본만 읽는 창구를 반환하는 함수.
    IPendingPathsQuery& pendingPaths();
    /// 파일 이동 없이 대기 목록에 등록하는 명령 창구를 반환하는 함수.
    IEnqueuePendingPaths& pendingCommands();
    /// 파일 이동 권한을 부여하지 않는 읽기 전용 분류 창구를 반환하는 함수.
    IClassificationQuery& classification();
    /// 정리·되돌리기·복구가 공유하는 단일 실행기를 반환하는 함수.
    BackgroundTaskRunner& tasks();
    /// 미리보기와 승인·정리 입력을 받는 화면 창구를 반환하는 함수.
    PreviewController& preview();
    /// 작업 기록 화면 창구를 반환하는 함수.
    HistoryController& history();
    /// 중단 작업 확인 화면 창구를 반환하는 함수.
    RecoveryController& recovery();
    /// 규칙 편집 화면 창구를 반환하는 함수.
    RuleEditorController& rules();
    /// 목적지 허용 설정 화면 창구를 반환하는 함수.
    DestinationSettingsController& destinations();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
