#include "app/feature_runtime_host.hpp"
#include "app/file_organizer_runtime.hpp"
#include "app/folder_watch_runtime.hpp"
#include "app/notification_runtime.hpp"
#include "app/companion_runtime.hpp"
#include "app/companion_feeding_runtime.hpp"
#include "app/feature_connections.hpp"
#include "app/ui_page_registry.hpp"
#include "presentation/pending_files_model.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/character_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QDir>

namespace nookbit {
/// 감시는 정리보다 먼저, 연결은 모든 기능보다 먼저 해제하는 내부 소유 상태.
struct FeatureRuntimeHost::State {
    std::unique_ptr<FileOrganizerRuntime> organizer;
    std::unique_ptr<FolderWatchRuntime> watch;
    std::unique_ptr<NotificationRuntime> notifications;
    std::unique_ptr<CompanionRuntime> companion;
    std::unique_ptr<CompanionFeedingRuntime> feeding;
    FeatureConnections routes;
    QObject connections;
    Result<void> result{Result<void>::success()};

    // 비활성 기능에 대체 일꾼을 만들지 않고 필요한 기능과 연결만 생성하는 함수.
    Result<void> initialize(const FeatureStartupPlan& plan, const FeatureRuntimeConfig& config,
        QSettings& preferences, QGuiApplication& app, UiPageRegistry& pages, FeatureRuntimeHost& host) {
        const auto active = plan.active;
        if (active.watch && !active.organizer)
            return Result<void>::failure({"feature.invalid_plan", "Folder Watch requires File Organizer."});
        if (active.feeding && !active.companion)
            return Result<void>::failure({"feature.invalid_plan", "Feeding requires Companion."});
        if (active.organizer) {
            organizer = std::make_unique<FileOrganizerRuntime>(FileOrganizerRuntimeConfig{
                config.appData, config.defaultDestination, config.verifyUi}, preferences);
            const auto added = organizer->registerUi(pages);
            if (!added) return added;
            QObject::connect(&organizer->tasks(), &BackgroundTaskRunner::busyChanged, &connections, [&host] { emit host.busyChanged(); });
        }
        if (active.watch) {
            watch = std::make_unique<FolderWatchRuntime>(FolderWatchRuntimeConfig{
                QDir(config.appData).filePath("watch.json"), ":/nookbit/watch.schema.json", config.watchFolder, !config.verifyUi},
                organizer->classification(), organizer->pendingPaths(), organizer->pendingCommands(), preferences);
            const auto added = watch->registerUi(pages);
            if (!added) return added;
            QObject::connect(&organizer->pendingFiles(), &PendingFilesModel::countChanged, &watch->controller(), &WatchController::pendingPathsChanged);
            QObject::connect(&organizer->rules(), &RuleEditorController::rulesSaved, &watch->controller(), &WatchController::rulesChanged);
        }
        if (active.notifications) {
            notifications = std::make_unique<NotificationRuntime>(preferences, !config.verifyUi);
            const auto added = notifications->registerUi(pages);
            if (!added) return added;
            if (watch) {
                const auto registered = notifications->controller().registerType(folderWatchOfferType());
                if (!registered) return registered;
            }
        }
        if (active.companion) {
            companion = std::make_unique<CompanionRuntime>(CompanionRuntimeConfig{
                config.appData, config.applicationDir, config.verifyUi}, preferences, app);
            if (!companion->startupResult()) return companion->startupResult();
            const auto added = companion->registerUi(pages);
            if (!added) return added;
            QObject::connect(&companion->imports(), &BackgroundTaskRunner::busyChanged, &connections, [&host] { emit host.busyChanged(); });
        }
        if (active.feeding) {
            feeding = std::make_unique<CompanionFeedingRuntime>(companion->interactionRegistry());
            if (!feeding->startupResult()) return feeding->startupResult();
        }
        // 표현 구독을 먼저 연결한 뒤 감시의 현재 사실을 발행하는 함수형 조립 순서.
        if (notifications && companion) {
            const auto bound = routes.notificationsToCompanion(notifications->controller(), companion->character(), companion->character());
            if (!bound) return bound;
        }
        if (organizer && companion) {
            const auto bound = routes.workToCompanion(organizer->tasks(), companion->character(), companion->character());
            if (!bound) return bound;
        }
        if (watch && notifications) {
            const auto bound = routes.watchToNotifications(watch->controller(), notifications->controller(), notifications->controller());
            if (!bound) return bound;
        }
        return Result<void>::success();
    }
};

// 외부 공유 객체의 소유권은 유지하고 기능별 소유 묶음만 조립하는 생성자.
FeatureRuntimeHost::FeatureRuntimeHost(FeatureStartupPlan plan, FeatureRuntimeConfig config,
    QSettings& preferences, QGuiApplication& app, UiPageRegistry& pages)
    : state_(std::make_unique<State>()) {
    state_->result = state_->initialize(plan, config, preferences, app, pages, *this);
}
// 내부 연결과 선택적인 기능을 선언 역순으로 해제하는 소멸자.
FeatureRuntimeHost::~FeatureRuntimeHost() = default;
// 시작 실패를 원래 오류 그대로 호스트에 돌려주는 함수.
const Result<void>& FeatureRuntimeHost::startupResult() const { return state_->result; }
// 존재하는 실행기의 상태만 조회하며 실행기를 새로 만들지 않는 함수.
bool FeatureRuntimeHost::busy() const {
    return (state_->organizer && state_->organizer->tasks().busy()) || (state_->companion && state_->companion->imports().busy());
}
// 활성 정리 기능의 수명 안에서만 사용할 포인터를 반환하는 함수.
FileOrganizerRuntime* FeatureRuntimeHost::organizer() const { return state_->organizer.get(); }
// 활성 감시 기능의 수명 안에서만 사용할 포인터를 반환하는 함수.
FolderWatchRuntime* FeatureRuntimeHost::watch() const { return state_->watch.get(); }
// 활성 알림 기능의 수명 안에서만 사용할 포인터를 반환하는 함수.
NotificationRuntime* FeatureRuntimeHost::notifications() const { return state_->notifications.get(); }
// 활성 컴패니언의 수명 안에서만 사용할 포인터를 반환하는 함수.
CompanionRuntime* FeatureRuntimeHost::companion() const { return state_->companion.get(); }
// 등록 소유 모듈을 빌려주며 컴패니언보다 먼저 해제하는 함수.
CompanionFeedingRuntime* FeatureRuntimeHost::feeding() const { return state_->feeding.get(); }
}
