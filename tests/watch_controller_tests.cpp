#include "presentation/pending_files_model.hpp"
#include "app/pending_queue_adapter.hpp"
#include "presentation/watch_controller.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "application/use_cases/classify_files_use_case.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>

#include <iostream>
#include <functional>

namespace {

int failures = 0;

// 예상 결과와 다르면 테스트 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트 파일의 절대 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

// 감시 테스트용 파일을 만드는 함수.
bool writeFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("photo bytes") == 11;
}

// 실제 조립부처럼 목록 변경을 감시 창구로 전달하는 함수.
void connectPendingChanges(nookbit::PendingFilesModel& pending, nookbit::WatchController& controller) {
    QObject::connect(&pending, &nookbit::PendingFilesModel::countChanged,
        &controller, &nookbit::WatchController::pendingPathsChanged);
}

// 조건 충족 제안은 대기 등록만 하고 같은 상태에서는 반복되지 않는지 검사하는 함수.
void testProposalAndPendingOnly() {
    QTemporaryDir directory;
    expect(directory.isValid(), "watch-controller directory should be available");
    if (!directory.isValid()) return;
    const auto folder = directory.path();
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    const auto third = directory.filePath(QStringLiteral("third.jpg"));
    const auto settingsPath = directory.filePath(QStringLiteral("watch.json"));
    expect(writeFile(first), "first watched file should be written");
    nookbit::PendingFilesModel pending;
    nookbit::PendingQueueAdapter queue(pending);
    nookbit::JsonWatchSettingsRepository repository(
        settingsPath, projectPath("schemas/watch.schema.json"));
    nookbit::JsonRuleSetRepository rules(
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine ruleEngine;
    nookbit::QtPlanHasher classificationHasher;
    nookbit::ClassifyFilesUseCase classification(rules, ruleEngine, classificationHasher);
    nookbit::EvaluateWatchFolderUseCase evaluateWatchFolder(scanner, classification, 0);
    nookbit::WatchController controller(
        queue, queue, repository, evaluateWatchFolder, folder);
    connectPendingChanges(pending, controller);
    controller.saveSettings(folder, true, false, 10, false,
                            true, 2, false, 1, false, 1, QStringLiteral("*.partial"));
    expect(controller.enabled() && controller.minimumFileCount() == 2 &&
               QFile::exists(settingsPath), "editable watch settings should be saved");
    controller.checkNow();
    expect(!controller.proposalVisible(), "one file should not meet a two-file threshold");
    expect(controller.lastEvaluation().value("fileCount").toULongLong() == 1
        && controller.lastEvaluation().value("totalBytes").toULongLong() == 11
        && !controller.lastEvaluation().value("shouldOffer").toBool()
        && !controller.lastEvaluation().value("checkedAt").toString().isEmpty(),
        "watch dashboard exposes measured values even before a threshold is met");
    expect(writeFile(second), "second watched file should be written");
    controller.checkNow();
    expect(controller.proposalVisible() && controller.proposalFiles().size() == 2 &&
               pending.count() == 0, "matching folder should propose without queueing files");
    controller.dismissProposal();
    controller.checkNow();
    expect(!controller.proposalVisible(), "dismissed unchanged proposal should not reappear");
    expect(writeFile(third), "third watched file should be written");
    controller.checkNow();
    expect(controller.proposalVisible() && controller.proposalFiles().size() == 3,
           "new file state should allow a new proposal");
    controller.addProposalToPending();
    expect(pending.count() == 3 && QFile::exists(first) && QFile::exists(second) &&
               QFile::exists(third) && !controller.proposalVisible(),
           "accepting a proposal should register paths without moving files");
    controller.checkNow();
    expect(!controller.proposalVisible(), "queued files must not be offered again");
    const auto fourth = directory.filePath(QStringLiteral("fourth.jpg"));
    const auto fifth = directory.filePath(QStringLiteral("fifth.jpg"));
    expect(writeFile(fourth), "fourth watched file should be written");
    controller.checkNow();
    expect(!controller.proposalVisible(),
           "three pending files and one new file must not meet a two-file threshold");
    expect(writeFile(fifth), "fifth watched file should be written");
    controller.checkNow();
    expect(controller.proposalVisible() && controller.proposalFiles().size() == 2 &&
               controller.proposalFiles().contains(fourth) && controller.proposalFiles().contains(fifth),
           "only the two new files should be offered");
    pending.addUrl(QUrl::fromLocalFile(fourth));
    expect(!controller.proposalVisible(), "manual queueing must invalidate a visible old proposal");
    controller.checkNow();
    expect(!controller.proposalVisible(), "one remaining candidate must not satisfy the threshold");
    pending.removeAt(pending.count() - 1);
    controller.checkNow();
    expect(controller.proposalVisible() && controller.proposalFiles().size() == 2,
           "removing a file from pending should make it eligible again");
    nookbit::PendingFilesModel restartedPending;
    nookbit::PendingQueueAdapter restartedQueue(restartedPending);
    restartedPending.restorePaths(pending.filePaths());
    nookbit::WatchController restarted(
        restartedQueue, restartedQueue, repository, evaluateWatchFolder, QString());
    connectPendingChanges(restartedPending, restarted);
    expect(restarted.enabled() && restarted.folder() == folder &&
               restarted.minimumFileCount() == 2 && restarted.rescanIntervalMinutes() == 10,
           "watch settings should reload after restarting the controller");
    restarted.checkNow();
    expect(restarted.proposalVisible() && restarted.proposalFiles().size() == 2 &&
               !restarted.proposalFiles().contains(first),
           "restored pending paths must be excluded after restarting the controller");
}

/// 실제 파일 검사에 테스트 관찰 시각을 전달하는 유스케이스 어댑터.
class TimedWatchUseCase final : public nookbit::IWatchProposalUseCase {
public:
    /// 기본 안정 대기 정책을 사용하는 유스케이스를 연결하는 생성자.
    explicit TimedWatchUseCase(nookbit::EvaluateWatchFolderUseCase& useCase) : useCase_(useCase) {}
    std::int64_t nowMs{100000};
    int calls{0};
    // 실제 파일을 가짜 시각으로 관찰하고 테스트 재검사 타이머만 단축하는 함수.
    nookbit::Result<nookbit::WatchProposal> execute(
        const nookbit::WatchSettings& settings, std::int64_t,
        const std::vector<std::filesystem::path>& pendingPaths) override {
        ++calls;
        auto result = useCase_.execute(settings, nowMs, pendingPaths);
        if (result && result.value().nextCheckDelayMs > 0) result.value().nextCheckDelayMs = 1;
        return result;
    }
private:
    nookbit::EvaluateWatchFolderUseCase& useCase_;
};

// 파일 변경 알림이 없어도 안정 대기가 끝나면 재검사하고 수락 전 변경은 막는지 검사하는 함수.
void testReadinessRetryAndAcceptance() {
    QTemporaryDir directory;
    expect(directory.isValid(), "readiness test directory should be available");
    if (!directory.isValid()) return;
    const auto file = directory.filePath(QStringLiteral("download.jpg"));
    expect(writeFile(file), "download test file should be created");
    nookbit::PendingFilesModel pending;
    nookbit::PendingQueueAdapter queue(pending);
    nookbit::JsonWatchSettingsRepository settings(
        directory.filePath(QStringLiteral("watch.json")), projectPath("schemas/watch.schema.json"));
    nookbit::JsonRuleSetRepository rules(projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::QtPlanHasher classificationHasher;
    nookbit::ClassifyFilesUseCase classification(rules, engine, classificationHasher);
    nookbit::EvaluateWatchFolderUseCase evaluate(scanner, classification);
    TimedWatchUseCase timed(evaluate);
    nookbit::WatchController controller(queue, queue, settings, timed, directory.path());
    connectPendingChanges(pending, controller);
    controller.saveSettings(directory.path(), true, false, 10, false,
                            true, 1, false, 1, false, 1, {});
    controller.checkNow();
    expect(!controller.proposalVisible() && controller.statusMessage().contains(QStringLiteral("확인 중")),
           "first observation must show waiting without proposing or queueing");
    timed.nowMs += 30000;
    QElapsedTimer timeout;
    timeout.start();
    while (!controller.proposalVisible() && timeout.elapsed() < 2500) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    expect(controller.proposalVisible() && pending.count() == 0,
           "readiness retry must produce a proposal without another directory change");
    QFile growing(file);
    expect(growing.open(QIODevice::Append), "download should reopen for writing");
    if (!growing.isOpen()) return;
    growing.write("more bytes");
    growing.close();
    timed.nowMs += 1000;
    controller.addProposalToPending();
    expect(pending.count() == 0 && !controller.proposalVisible(),
           "acceptance must reject a file that resumed writing after the proposal");
    timed.nowMs += 30000;
    controller.checkNow();
    expect(controller.proposalVisible(), "completed download should become eligible after a new stable window");
    controller.addProposalToPending();
    expect(pending.count() == 1 && QFile::exists(file),
           "stable accepted files should only enter pending without movement");
}

// 나중에 알림 중 새 파일·규칙 변경은 제안하지 않고 만료 때 현재 후보를 다시 검사하는 함수.
void testSnoozeAndCurrentCandidates() {
    QTemporaryDir directory;
    expect(directory.isValid(), "snooze test directory should be available");
    if (!directory.isValid()) return;
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    expect(writeFile(first), "snooze source should be created");
    nookbit::PendingFilesModel pending;
    nookbit::PendingQueueAdapter queue(pending);
    nookbit::JsonWatchSettingsRepository settings(
        directory.filePath(QStringLiteral("watch.json")), projectPath("schemas/watch.schema.json"));
    nookbit::JsonRuleSetRepository rules(projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::QtPlanHasher classificationHasher;
    nookbit::ClassifyFilesUseCase classification(rules, engine, classificationHasher);
    nookbit::EvaluateWatchFolderUseCase evaluate(scanner, classification, 0);
    TimedWatchUseCase timed(evaluate);
    nookbit::WatchController controller(queue, queue, settings, timed, directory.path());
    connectPendingChanges(pending, controller);
    controller.saveSettings(directory.path(), true, false, 10, false,
                            true, 1, false, 1, false, 1, {});
    controller.checkNow();
    expect(controller.proposalVisible(), "a qualifying folder should offer before snoozing");
    const auto before = QDateTime::currentMSecsSinceEpoch();
    qint64 savedDeadline = 0;
    QObject::connect(&controller, &nookbit::WatchController::snoozeDeadlineChanged,
        &controller, [&savedDeadline](qint64 deadline) { savedDeadline = deadline; });
    controller.snoozeProposal();
    expect(!controller.proposalVisible() && savedDeadline >= before + 30 * 60000 &&
               savedDeadline <= QDateTime::currentMSecsSinceEpoch() + 30 * 60000 &&
               pending.count() == 0 && QFile::exists(first),
           "snoozing should publish a thirty-minute deadline without touching files or queue");
    const auto calls = timed.calls;
    expect(writeFile(second), "a new file should appear during snooze");
    controller.checkNow();
    controller.rulesChanged();
    expect(!controller.proposalVisible() && timed.calls == calls,
           "snooze should suppress new file and rule triggers without scanning");
    pending.addUrl(QUrl::fromLocalFile(first));
    controller.checkNow();
    expect(controller.snoozedUntilUtcMs() == savedDeadline && timed.calls == calls,
           "queue changes must not cancel snooze or trigger scans");

    nookbit::PendingFilesModel restoredPending;
    nookbit::PendingQueueAdapter restoredQueue(restoredPending);
    restoredPending.restorePaths(pending.filePaths());
    nookbit::WatchController restored(restoredQueue, restoredQueue, settings, timed, directory.path());
    connectPendingChanges(restoredPending, restored);
    restored.restoreSnoozeUntil(savedDeadline);
    restored.checkNow();
    expect(!restored.proposalVisible() && restored.snoozedUntilUtcMs() > 0 && timed.calls == calls,
           "restored snooze must suppress startup checks on the same folder");

    // 30분을 실제 기다리지 않고 짧은 저장 시각을 복원해 같은 만료 흐름을 검사한다.
    controller.restoreSnoozeUntil(QDateTime::currentMSecsSinceEpoch() + 150);
    QElapsedTimer timeout;
    timeout.start();
    while (!controller.proposalVisible() && timeout.elapsed() < 1500) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    expect(controller.proposalVisible() && controller.snoozedUntilUtcMs() == 0 && savedDeadline == 0 &&
               controller.proposalFiles().size() == 1 && controller.proposalFiles().contains(second) &&
               pending.count() == 1 && QFile::exists(first),
           "expiry must reevaluate current files, exclude queued paths and clear persisted deadline");

    controller.snoozeProposal();
    controller.restoreSnoozeUntil(QDateTime::currentMSecsSinceEpoch() + 100);
    expect(QFile::remove(second), "owned snooze fixture should be removed");
    timeout.restart();
    while (controller.snoozedUntilUtcMs() != 0 && timeout.elapsed() < 1500) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    expect(!controller.proposalVisible() && controller.snoozedUntilUtcMs() == 0,
           "expiry must not reopen stale proposals for deleted or queued files");
    expect(writeFile(second), "unchanged reminder fixture should be recreated");
    controller.checkNow();
    expect(controller.proposalVisible(), "recreated candidate should offer before the reminder test");
    const auto unchangedProposal = controller.proposalFiles();
    controller.snoozeProposal();
    controller.restoreSnoozeUntil(QDateTime::currentMSecsSinceEpoch() + 100);
    timeout.restart();
    while (!controller.proposalVisible() && timeout.elapsed() < 1500) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    expect(controller.proposalVisible() && controller.proposalFiles() == unchangedProposal,
           "expiry must remind even when the same candidate snapshot has not changed");
    controller.restoreSnoozeUntil(QDateTime::currentMSecsSinceEpoch() + 60000);
    controller.saveSettings(directory.path(), false, false, 10, false,
                            true, 1, false, 1, false, 1, {});
    expect(controller.snoozedUntilUtcMs() == 0 && !controller.enabled(),
           "turning monitoring off must cancel snooze and its timer");
    QTemporaryDir otherFolder;
    expect(otherFolder.isValid(), "another watched folder should be available");
    if (!otherFolder.isValid()) return;
    controller.restoreSnoozeUntil(QDateTime::currentMSecsSinceEpoch() + 60000);
    controller.saveSettings(otherFolder.path(), true, false, 10, false,
                            true, 1, false, 1, false, 1, {});
    expect(controller.snoozedUntilUtcMs() == 0,
           "switching watched folders must cancel the old folder snooze");
}

// 화면에서 고른 크기 숫자·단위를 저장하고 다시 열어도 같은 값인지 검사하는 함수.
void testSizeUnitSettings() {
    QTemporaryDir directory;
    nookbit::PendingFilesModel pending;
    nookbit::PendingQueueAdapter queue(pending);
    nookbit::JsonWatchSettingsRepository repository(directory.filePath("watch.json"), projectPath("schemas/watch.schema.json"));
    nookbit::JsonRuleSetRepository rules(projectPath("resources/rules/default.example.json"), projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::QtPlanHasher classificationHasher;
    nookbit::ClassifyFilesUseCase classification(rules, engine, classificationHasher);
    nookbit::EvaluateWatchFolderUseCase evaluate(scanner, classification, 0);
    nookbit::WatchController controller(queue, queue, repository, evaluate, directory.path());
    connectPendingChanges(pending, controller);
    for (const auto* unit : {"KB", "MB", "GB", "MiB"}) {
        controller.saveSettings(directory.path(), false, false, 10, false, false, 1, true, 37, false, 1, {}, QString::fromUtf8(unit));
        nookbit::WatchController restored(queue, queue, repository, evaluate, directory.path());
        connectPendingChanges(pending, restored);
        expect(restored.sizeEnabled() && restored.minimumTotalSize() == 37 && restored.totalSizeUnit() == QString::fromUtf8(unit),
               "controller restores exact size number and selected unit");
    }
}
// 화면 모델이 없는 조회·등록 창구로 감시 흐름을 시험하는 대리 객체.
class FakePendingQueue final : public nookbit::IPendingPathsQuery, public nookbit::IEnqueuePendingPaths {
public:
    bool queryFails{false}, enqueueFails{false};
    std::vector<std::filesystem::path> paths, received;
    std::function<void()> whileEnqueueing;
    /// 조회 오류 또는 값 복사본을 반환하는 함수.
    nookbit::Result<std::vector<std::filesystem::path>> pendingPaths() const override {
        if (queryFails) return nookbit::Result<std::vector<std::filesystem::path>>::failure({"test.query", "test query failure"});
        return nookbit::Result<std::vector<std::filesystem::path>>::success(paths);
    }
    /// 변경 알림 도중에도 등록 요청의 값과 부분 성공 결과를 유지하는 함수.
    nookbit::Result<nookbit::PendingEnqueueResult> enqueuePaths(const std::vector<std::filesystem::path>& input) override {
        if (enqueueFails) return nookbit::Result<nookbit::PendingEnqueueResult>::failure({"test.enqueue", "test enqueue failure"});
        received = input;
        if (whileEnqueueing) whileEnqueueing();
        nookbit::PendingEnqueueResult result;
        if (!input.empty()) { paths.push_back(input.front()); result.added.push_back(input.front()); }
        for (std::size_t index = 1; index < input.size(); ++index) result.rejected.push_back({input[index], {"test.rejected", "test rejection"}});
        return nookbit::Result<nookbit::PendingEnqueueResult>::success(std::move(result));
    }
};

class FakeWatchEvaluation final : public nookbit::IWatchProposalUseCase {
public:
    int calls{};
    std::vector<std::filesystem::path> seenPending;
    nookbit::WatchProposal proposal;
    /// 전달된 대기 경로를 보관하고 고정된 제안을 반환하는 함수.
    nookbit::Result<nookbit::WatchProposal> execute(const nookbit::WatchSettings&, std::int64_t,
        const std::vector<std::filesystem::path>& pending) override {
        ++calls; seenPending = pending;
        return nookbit::Result<nookbit::WatchProposal>::success(proposal);
    }
};

// 대기 목록 화면 객체 없이도 조회 오류·등록 오류·등록 중 제안 무효화와 부분 성공을 처리하는지 검사하는 함수.
void testPendingQueueBoundary() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "queue-boundary fixture"); return; }
    nookbit::JsonWatchSettingsRepository repository(directory.filePath("watch.json"), projectPath("schemas/watch.schema.json"));
    FakePendingQueue queue; FakeWatchEvaluation evaluation;
    for (const auto* name : {"one.jpg", "two.jpg"}) {
        const auto path = directory.filePath(QString::fromLatin1(name));
        expect(writeFile(path), "boundary source file");
        nookbit::FileFact fact; fact.absolutePath = std::filesystem::path(path.toStdWString()); fact.fingerprint.size = 11;
        evaluation.proposal.eligibleFiles.push_back(fact);
    }
    evaluation.proposal.evaluation.shouldOffer = true;
    evaluation.proposal.evaluation.fileCount = 2;
    nookbit::WatchController controller(queue, queue, repository, evaluation, directory.path());
    controller.saveSettings(directory.path(), false, false, 10, false, true, 1, false, 1, false, 1, {});
    queue.paths = {std::filesystem::path(directory.filePath("already-pending.jpg").toStdWString())};
    controller.checkNow();
    expect(controller.proposalVisible() && evaluation.seenPending == queue.paths, "watch reads queue through its query contract");
    const auto calls = evaluation.calls;
    queue.queryFails = true; controller.checkNow();
    expect(!controller.proposalVisible() && evaluation.calls == calls && controller.statusMessage().contains("test query failure"),
        "query failure withdraws stale offer and does not evaluate with an empty fallback");
    queue.queryFails = false; controller.pendingPathsChanged(); controller.checkNow();
    queue.enqueueFails = true; controller.addProposalToPending();
    expect(controller.proposalVisible() && queue.received.empty() && controller.statusMessage().contains("test enqueue failure"),
        "command failure is visible and leaves the offer available to retry");
    queue.enqueueFails = false;
    queue.whileEnqueueing = [&] { controller.pendingPathsChanged(); };
    controller.addProposalToPending();
    expect(queue.received.size() == 2 && !controller.proposalVisible() && controller.statusMessage().contains("test rejection"),
        "copied proposal survives synchronous invalidation and reports rejected paths");
    expect(QFile::exists(directory.filePath("one.jpg")) && QFile::exists(directory.filePath("two.jpg")),
        "accepting an offer never moves the original files");
}
// 파일 집합이 같아도 규칙 변경 뒤에는 제안을 새로 확인해야 하는지 검사하는 함수.
void testProposalRuleRevision() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "revision fixture"); return; }
    const auto path = directory.filePath("same.jpg");
    expect(writeFile(path), "revision source file");
    FakePendingQueue queue; FakeWatchEvaluation evaluation;
    nookbit::FileFact file; file.absolutePath = std::filesystem::path(path.toStdWString());
    evaluation.proposal.eligibleFiles = {file};
    evaluation.proposal.evaluation.shouldOffer = true;
    evaluation.proposal.evaluation.fileCount = 1;
    evaluation.proposal.rulesRevision = "rules-a";
    nookbit::JsonWatchSettingsRepository repository(directory.filePath("watch.json"), projectPath("schemas/watch.schema.json"));
    nookbit::WatchController controller(queue, queue, repository, evaluation, directory.path());
    controller.saveSettings(directory.path(), false, false, 10, false, true, 1, false, 1, false, 1, {});
    controller.checkNow(); controller.dismissProposal();
    evaluation.proposal.rulesRevision = "rules-b";
    controller.checkNow();
    expect(controller.proposalVisible(), "new rules allow a fresh offer even for unchanged files");
    evaluation.proposal.rulesRevision = "rules-c";
    controller.addProposalToPending();
    expect(controller.proposalVisible() && queue.received.empty() && controller.statusMessage().contains(QStringLiteral("분류 규칙")),
        "acceptance recheck detects changed rules and does not enqueue the old offer");
    controller.addProposalToPending();
    expect(!controller.proposalVisible() && queue.received.size() == 1 && QFile::exists(path),
        "confirming the fresh offer only enqueues and leaves its source untouched");
}
} // namespace

// 폴더 감시 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testProposalAndPendingOnly();
    testReadinessRetryAndAcceptance();
    testSnoozeAndCurrentCandidates();
    testSizeUnitSettings();
    testPendingQueueBoundary();
    testProposalRuleRevision();
    if (failures == 0) {
        std::cout << "All watch controller tests passed.\n";
        return 0;
    }
    std::cerr << failures << " watch controller assertion(s) failed.\n";
    return 1;
}
