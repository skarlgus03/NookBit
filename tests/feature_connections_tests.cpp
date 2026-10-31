#include "app/feature_connections.hpp"
#include "presentation/notification_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {
int failures{};
// 예상과 다른 연결 동작을 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

class CharacterRecipient final : public QObject, public nookbit::ICharacterChannel, public nookbit::ICharacterReactionChannel {
public:
    bool fail{false};
    std::vector<nookbit::CharacterUpdate> received;
    std::vector<nookbit::CharacterReaction> reactions;
    // 표시 요청을 기록하고 선택적으로 전달 실패를 반환하는 함수.
    nookbit::Result<void> publish(const nookbit::CharacterUpdate& value) override {
        received.push_back(value);
        return fail ? nookbit::Result<void>::failure({"test.delivery", "Injected presentation delivery failure."}) : nookbit::Result<void>::success();
    }
    // 일시 표현의 식별자와 표시 시간만 기록하고 업무에는 접근하지 않는 함수.
    nookbit::Result<void> present(const nookbit::CharacterReaction& value) override {
        reactions.push_back(value);
        return fail ? nookbit::Result<void>::failure({"test.delivery", "Injected reaction delivery failure."}) : nookbit::Result<void>::success();
    }
};

class Queue final : public nookbit::IPendingPathsQuery, public nookbit::IEnqueuePendingPaths {
public:
    int enqueues{};
    // 파일 이동과 무관한 빈 대기 목록을 반환하는 함수.
    nookbit::Result<std::vector<std::filesystem::path>> pendingPaths() const override {
        return nookbit::Result<std::vector<std::filesystem::path>>::success({});
    }
    // 알림 연결이 등록 작업을 실행하는지 감지하는 함수.
    nookbit::Result<nookbit::PendingEnqueueResult> enqueuePaths(const std::vector<std::filesystem::path>&) override {
        ++enqueues; return nookbit::Result<nookbit::PendingEnqueueResult>::success({});
    }
};

class WatchSettings final : public nookbit::IWatchSettingsRepository {
public:
    mutable nookbit::WatchSettings value;
    // 메모리에 있는 감시 설정을 반환하는 함수.
    nookbit::Result<nookbit::WatchSettings> load() const override { return nookbit::Result<nookbit::WatchSettings>::success(value); }
    // 테스트 설정만 변경하는 함수.
    nookbit::Result<void> save(const nookbit::WatchSettings& settings) const override {
        value = settings; return nookbit::Result<void>::success();
    }
};

class WatchEvaluation final : public nookbit::IWatchProposalUseCase {
public:
    // 파일 시스템을 읽지 않고 현재 제안을 제공하는 함수.
    nookbit::Result<nookbit::WatchProposal> execute(const nookbit::WatchSettings& settings, std::int64_t,
        const std::vector<std::filesystem::path>&) override {
        nookbit::WatchProposal result; result.evaluation.shouldOffer = true; result.evaluation.fileCount = 1;
        result.rulesRevision = "test-rules";
        nookbit::FileFact file; file.absolutePath = settings.folder / "photo.jpg";
        result.eligibleFiles.push_back(file);
        return nookbit::Result<nookbit::WatchProposal>::success(std::move(result));
    }
};

// 실제 기존 작업 실행기의 성공·실패 결과를 기다리고 제한 시간을 확인하는 함수.
bool runTask(nookbit::BackgroundTaskRunner& tasks, nookbit::BackgroundTaskRunner::TaskKind kind, bool succeeds = true) {
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    bool completed = false, resultValue = false;
    const bool accepted = tasks.submit(&loop, [succeeds] {
        // 표시 결과와 독립적인 업무 결과를 반환하는 함수.
        return succeeds ? nookbit::Result<void>::success() : nookbit::Result<void>::failure({"test.work", "Work failed."});
    }, [&](nookbit::Result<void> result) {
        // 전달받은 작업 결과를 기록하고 대기를 종료하는 함수.
        completed = true; resultValue = bool(result); loop.quit();
    }, kind);
    timeout.start(2000);
    if (accepted) loop.exec();
    expect(accepted && completed && !tasks.busy(), "background work completes within its test lifetime");
    return resultValue;
}

// 초기 제안 전달·표시 음소거·업무 활동 독립성과 실패 분리를 검사하는 함수.
void testRoutes() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "route fixture"); return; }
    Queue queue; WatchSettings settings; WatchEvaluation evaluation;
    settings.value.folder = std::filesystem::path(directory.path().toStdWString());
    settings.value.conditions = {{nookbit::WatchConditionType::FileCount, 1}};
    nookbit::WatchController watch(queue, queue, settings, evaluation, directory.path());
    watch.checkNow();
    nookbit::NotificationController notifications([](const QString&, bool fallback) { return fallback; },
        [](const QString&, bool) { return nookbit::Result<void>::success(); });
    expect(notifications.registerType(nookbit::folderWatchOfferType()).hasValue(), "existing notification ID registers");
    CharacterRecipient companion; nookbit::BackgroundTaskRunner tasks; nookbit::FeatureConnections connections;
    expect(connections.notificationsToCompanion(notifications, companion, companion).hasValue(), "bind notification consumer");
    expect(connections.workToCompanion(tasks, companion, companion).hasValue(), "bind independent file activity");
    expect(connections.workResultsToCompanion(tasks, companion, companion).hasValue(), "bind independent file result cues");
    expect(companion.received.size() == 1 && !companion.received[0].active, "current idle work state is synchronized");
    expect(connections.watchToNotifications(watch, notifications, notifications).hasValue(), "bind watch source after its consumer");
    expect(companion.received.size() == 2 && companion.received.back().active && companion.received.back().sourceId == "folder-watch" &&
        companion.received.back().entryId == "file.offer/organization-offer" && companion.received.back().cueId == "file.offer" &&
        companion.received.back().kind == nookbit::CharacterUpdateKind::Attention, "current offer is delivered with stable identity");
    const auto beforeRepeat = companion.received.size();
    watch.checkNow();
    expect(companion.received.size() == beforeRepeat, "repeated watch state does not replay attention");
    expect(notifications.setEnabled("file.offer", false) && !companion.received.back().active && watch.proposalVisible(),
        "muting withdraws attention without cancelling the source proposal");
    companion.received.clear();
    expect(runTask(tasks, nookbit::BackgroundTaskRunner::TaskKind::Inspection), "inspection succeeds");
    bool workActive = false;
    for (const auto& update : companion.received) workActive = workActive || update.active;
    expect(!workActive, "read-only inspection never produces active file work");
    expect(companion.reactions.empty(), "inspection cannot produce completion or failure animations");
    companion.received.clear();
    expect(runTask(tasks, nookbit::BackgroundTaskRunner::TaskKind::FileWork), "file work succeeds while notifications are muted");
    expect(companion.reactions.size() == 1 && companion.reactions.back().cueId == "file.work.completed"
        && companion.reactions.back().durationMs == 2400, "successful file work emits one bounded completion cue even when notifications are muted");
    expect(companion.received.size() == 2 && companion.received[0].active && !companion.received[1].active &&
        companion.received[0].kind == nookbit::CharacterUpdateKind::Activity && companion.received[0].sourceId == "file-operations" &&
        companion.received[0].entryId == "current-operation" && companion.received[0].cueId == "file.work", "file activity starts and stops independently of notification preferences");
    expect(notifications.setEnabled("file.offer", true) && companion.received.back().active, "reenabling restores current attention");
    watch.dismissProposal();
    expect(notifications.activeTypes().isEmpty() && !companion.received.back().active && queue.enqueues == 0,
        "withdrawal clears attention and connection code never enqueues files");
    companion.fail = true;
    expect(runTask(tasks, nookbit::BackgroundTaskRunner::TaskKind::FileWork), "presentation failure cannot change successful work");
    companion.fail = false;
    expect(!runTask(tasks, nookbit::BackgroundTaskRunner::TaskKind::FileWork, false) && !companion.received.back().active,
        "failed file work still clears its active presentation state");
    expect(companion.reactions.back().cueId == "file.work.failed", "failed file work emits a distinct failure cue");
}

// 다른 스레드에서 신호만 발생시켜 연결부에 대기 중 전달을 만드는 함수.
void queueNotice(nookbit::NotificationController& source) {
    std::unique_ptr<QThread> emitter(QThread::create([&source] {
        source.notificationChanged("file.offer", "folder-watch", "queued", true, "Title", "Message");
    }));
    emitter->start(); emitter->wait();
}

// 연결부·발신자·수신자 해제 뒤 남은 신호가 사라진 객체에 접근하지 않는지 검사하는 함수.
void testLifetimes() {
    nookbit::NotificationController notifications([](const QString&, bool fallback) { return fallback; },
        [](const QString&, bool) { return nookbit::Result<void>::success(); });
    CharacterRecipient recipient;
    {
        auto connections = std::make_unique<nookbit::FeatureConnections>();
        expect(connections->notificationsToCompanion(notifications, recipient, recipient).hasValue(), "lifetime binding");
        queueNotice(notifications);
        connections.reset();
        QCoreApplication::processEvents();
        expect(recipient.received.empty(), "destroying connection owner drops queued delivery");
    }
    nookbit::FeatureConnections connections;
    nookbit::BackgroundTaskRunner resultSource;
    {
        auto temporary = std::make_unique<nookbit::FeatureConnections>();
        expect(bool(temporary->workResultsToCompanion(resultSource, recipient, recipient)), "result lifetime binding");
        std::unique_ptr<QThread> emitter(QThread::create([&] { emit resultSource.fileWorkFinished(true); }));
        emitter->start(); emitter->wait();
        temporary.reset(); QCoreApplication::processEvents();
        expect(recipient.reactions.empty(), "destroying the result route drops queued delivery");
    }
    auto temporaryRecipient = std::make_unique<CharacterRecipient>();
    expect(connections.notificationsToCompanion(notifications, *temporaryRecipient, *temporaryRecipient).hasValue(), "temporary recipient binding");
    queueNotice(notifications); temporaryRecipient.reset(); QCoreApplication::processEvents();
    auto source = std::make_unique<nookbit::NotificationController>([](const QString&, bool fallback) { return fallback; },
        [](const QString&, bool) { return nookbit::Result<void>::success(); });
    expect(connections.notificationsToCompanion(*source, recipient, recipient).hasValue(), "temporary source binding");
    queueNotice(*source); source.reset(); QCoreApplication::processEvents();
    expect(recipient.received.empty(), "destroyed source is not dereferenced during queued delivery");
    bool rejected = false;
    std::unique_ptr<QThread> worker(QThread::create([&] {
        nookbit::FeatureConnections foreign;
        const auto result = foreign.notificationsToCompanion(notifications, recipient, recipient);
        rejected = !result && result.error().code == "connections.wrong_thread";
    }));
    worker->start(); worker->wait();
    expect(rejected, "binding across owning threads is rejected without touching either channel");
}

// 컴패니언 연결이 없어도 감시와 알림 기능이 동작하는지 검사하는 함수.
void testWithoutCompanion() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "optional consumer fixture"); return; }
    Queue queue; WatchSettings settings; WatchEvaluation evaluation;
    settings.value.folder = std::filesystem::path(directory.path().toStdWString());
    settings.value.conditions = {{nookbit::WatchConditionType::FileCount, 1}};
    nookbit::WatchController watch(queue, queue, settings, evaluation, directory.path());
    nookbit::NotificationController notifications([](const QString&, bool fallback) { return fallback; },
        [](const QString&, bool) { return nookbit::Result<void>::success(); });
    expect(notifications.registerType(nookbit::folderWatchOfferType()).hasValue(), "standalone offer registration");
    nookbit::FeatureConnections connections;
    expect(connections.watchToNotifications(watch, notifications, notifications).hasValue(), "watch binds without a companion");
    watch.checkNow();
    expect(watch.proposalVisible() && notifications.activeTypes().contains("file.offer") && queue.enqueues == 0,
        "missing companion does not block an offer or execute work");
}
}

// 실제 UI 없이 기능 간 연결과 수명 경계를 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testRoutes(); testLifetimes(); testWithoutCompanion();
    return failures ? 1 : 0;
}
