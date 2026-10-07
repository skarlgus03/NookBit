#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/filesystem/file_operation_service.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include "infrastructure/filesystem/interrupted_operation_recovery_service.hpp"
#include "infrastructure/filesystem/windows_transfer_recovery.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/configuration/runtime_settings_repository.hpp"
#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <QSaveFile>
#include <QEventLoop>
#include <QTimer>
#include <QSemaphore>
#include <atomic>
#include "app/thread_local_operations.hpp"
#include <QDateTime>
#include <QUuid>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;
void expect(bool condition, const std::string& message);

// 화면 이벤트를 계속 처리하면서 정해진 시간 안에 작업 완료를 기다리는 함수.
bool waitForBackground(nookbit::BackgroundTaskRunner& runner) {
    if (!runner.busy()) return true;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&runner, &nookbit::BackgroundTaskRunner::busyChanged, &loop, [&] { if (!runner.busy()) loop.quit(); });
    deadline.start(30000); loop.exec();
    return !runner.busy();
}

// 화면 반응·단일 실행·결과 스레드·수신자 소멸·예외 처리를 검사하는 함수.
void testBackgroundRunner() {
    nookbit::BackgroundTaskRunner runner;
    QObject receiver;
    QSemaphore gate;
    std::atomic<bool> otherThread{false};
    bool completed = false, heartbeat = false;
    auto* screenThread = QThread::currentThread();
    expect(runner.submit(&receiver, [&] {
        otherThread = QThread::currentThread() != screenThread;
        gate.acquire(); return nookbit::Result<void>::success();
    }, [&](const nookbit::Result<void>& result) {
        completed = result.hasValue(); expect(QThread::currentThread() == screenThread, "completion must run on screen thread");
    }), "background job must be accepted");
    expect(runner.busy() && !runner.submit(&receiver, [] { return nookbit::Result<void>::success(); }, [](const auto&) {}), "busy job must refuse duplicate");
    QTimer::singleShot(0, &receiver, [&] { heartbeat = true; gate.release(); });
    expect(waitForBackground(runner) && completed && heartbeat && otherThread, "UI events must run while work is on another thread");
    QObject* removed = new QObject;
    bool obsoleteCallback = false;
    runner.submit(removed, [] { return nookbit::Result<void>::success(); }, [&](const auto&) { obsoleteCallback = true; });
    delete removed;
    expect(waitForBackground(runner) && !obsoleteCallback, "deleted controller must not receive completion");
    bool exceptionReported = false;
    runner.submit(&receiver, []() -> nookbit::Result<void> { throw std::runtime_error("owned test exception"); },
        [&](const auto& result) { exceptionReported = !result && result.error().code == "background.exception"; });
    expect(waitForBackground(runner) && exceptionReported, "exception must release busy state and report failure");
    std::atomic<bool> drained{false};
    {
        nookbit::BackgroundTaskRunner finishing;
        finishing.submit(&receiver, [&] { drained = true; return nookbit::Result<void>::success(); }, [](const auto&) {});
    }
    expect(drained, "runner destruction must wait for worker instead of terminating it");
}

// 테스트가 미리보기 중간에 설정을 바꿀 수 있게 하는 저장소.
class MutableSettingsRepository final : public nookbit::ISettingsRepository {
public:
    nookbit::UserSettings settings;

    // 현재 테스트 설정을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::UserSettings> load() const override {
        return nookbit::Result<nookbit::UserSettings>::success(settings);
    }

    // 테스트에서는 설정을 파일에 쓰지 않는 함수.
    [[nodiscard]] nookbit::Result<void> save(const nookbit::UserSettings&) override {
        return nookbit::Result<void>::failure({"test.read_only", "Not used in this test."});
    }
};

// 테스트에서 준비한 규칙을 그대로 반환하는 저장소.
class MemoryRulesRepository final : public nookbit::IRuleSetRepository {
public:
    nookbit::RuleSet rules;

    // 현재 테스트 규칙을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::RuleSet> load() const override {
        return nookbit::Result<nookbit::RuleSet>::success(rules);
    }
};

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트의 예제 규칙 및 Schema 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

/// 화면 테스트에서 미리보기 일꾼과 유스케이스의 수명을 함께 관리한다.
struct PreviewWorkflow {
    nookbit::JsonRuleSetRepository rules;
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine ruleEngine;
    nookbit::Planner planner;
    nookbit::QtPlanHasher hasher;
    nookbit::WindowsStagedCopy copy;
    nookbit::WindowsTransferSourceRemoval removal;
    nookbit::WindowsTransferFileInspector files;
    nookbit::CreatePreviewUseCase useCase;
    nookbit::SqliteHistoryRepository history;
    nookbit::FreezePreviewUseCase freezeUseCase;
    nookbit::VerifySavedPlanUseCase verifyUseCase;
    nookbit::ApprovePlanUseCase approveUseCase;
    nookbit::SqliteTransferJournal journal;
    nookbit::ExecuteTransferStepUseCase forward;
    nookbit::RollbackTransferUseCase reverse;
    nookbit::FileOperationService operations;

    /// 테스트 규칙과 보호 경로로 미리보기 작업을 조립하는 생성자.
    PreviewWorkflow(QString rulesPath, QString schemaPath, std::filesystem::path appData, bool native = false)
        : rules(std::move(rulesPath), std::move(schemaPath)),
          planner(ruleEngine, appData, native ? nookbit::CrossVolumeDrafts::Allow : nookbit::CrossVolumeDrafts::Reject),
          useCase(rules, scanner, planner, &hasher, native ? &copy : nullptr),
          history(QString::fromStdWString((appData.parent_path() / L"history.sqlite").wstring()),
                  projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql")),
          freezeUseCase(rules, scanner, planner, hasher, history, native ? &copy : nullptr),
          verifyUseCase(rules, scanner, planner, hasher, history, native ? &copy : nullptr),
          approveUseCase(verifyUseCase, history),
          journal(QString::fromStdWString((appData.parent_path() / L"history.sqlite").wstring()),
              projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql")),
          forward(copy, removal, journal, history, hasher),
          reverse(journal, history, copy, removal, files, hasher),
          operations(verifyUseCase, scanner, hasher, history, std::move(appData), &copy, &forward, &reverse) {}

    /// 기본 예제 규칙으로 미리보기 작업을 조립하는 생성자.
    explicit PreviewWorkflow(std::filesystem::path appData)
        : PreviewWorkflow(projectPath("resources/rules/default.example.json"),
                          projectPath("schemas/rules.schema.json"), std::move(appData)) {}
};

// 독립 DB 연결로 이동·두 종류 되돌리기·중단 재개를 비동기 검사하는 함수.
void testBackgroundWorkflow(const QString& sourceRoot, const QString& targetRoot) {
#ifdef _WIN32
    const auto home = QDir(targetRoot).filePath("organized");
    const auto appData = std::filesystem::path(QDir(targetRoot).filePath("app-data").toStdWString());
    const auto rules = projectPath("resources/rules/default.v3.json"), schema = projectPath("schemas/rules-tree-v3.schema.json");
    PreviewWorkflow workflow(rules, schema, appData, true);
    nookbit::UserSettings settings; settings.homeRoot = std::filesystem::path(home.toStdWString());
    nookbit::RuntimeSettingsRepository current(settings);
    nookbit::ThreadLocalOperations operations(QDir(targetRoot).filePath("history.sqlite"),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"), rules, schema, {}, appData, current);
    nookbit::BackgroundTaskRunner runner;
    nookbit::PendingFilesModel pending;
    nookbit::PreviewController preview(pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, operations, rules, schema, home);
    nookbit::HistoryController history(workflow.history, operations);
    nookbit::RecoveryController recovery(workflow.history, nullptr, &workflow.journal, &operations);
    preview.setTaskRunner(&runner); history.setTaskRunner(&runner); recovery.setTaskRunner(&runner);
    preview.setSortWorkflow(&operations);
    QObject::connect(&history, &nookbit::HistoryController::rollbackAttempted,
        &preview, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    const QStringList sources{QDir(sourceRoot).filePath("background.jpg"), QDir(targetRoot).filePath("local.png")};
    for (const auto& source : sources) {
        QFile file(source); expect(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(QByteArray(2 * 1024 * 1024 + 37, 'b')) > 0, "owned background source");
        file.close(); pending.addUrl(QUrl::fromLocalFile(source));
    }
    const bool taggedCrossVolume = sourceRoot != targetRoot;
    const QByteArray ownedMarker("[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.invalid/owned-background\r\n");
    const auto ownedZone = sources[0] + ":Zone.Identifier";
    if (taggedCrossVolume) { QFile marker(ownedZone); expect(marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) && marker.write(ownedMarker) == ownedMarker.size(), "owned cross-volume marker"); }
    preview.createPreview(home);
    expect(!preview.prepareSort() && runner.busy() && !preview.awaitingConfirmation(), "preparation must return immediately without opening premature confirmation");
    preview.confirmSort();
    expect(waitForBackground(runner) && preview.awaitingConfirmation() && !preview.hasApprovedPlan(), "background preparation opens confirmation without approval");
    expect(preview.message().contains(QStringLiteral("검사 ")) && preview.message().contains(QStringLiteral("초")),
        "background preparation displays automatic elapsed time");
    QFile prepareTiming(QString::fromStdWString((appData / "diagnostics" / "last-prepare-timing.log").wstring()));
    expect(prepareTiming.open(QIODevice::ReadOnly) && prepareTiming.readAll().contains("operation=prepare outcome=success"),
        "background preparation writes a separate timing report");
    prepareTiming.close();
    for (const auto& source : sources) expect(QFile::exists(source), "preparation must not move files");
    preview.cancelSort(); expect(!preview.awaitingConfirmation() && !runner.busy(), "cancel preparation confirmation without movement");
    preview.prepareSort(); expect(waitForBackground(runner) && preview.awaitingConfirmation(), "frozen plan recheck in background");
    preview.confirmSort(); expect(runner.busy(), "sort must return before background completion");
    preview.confirmSort(); preview.undoLastMove();
    expect(waitForBackground(runner) && pending.count() == 0 && preview.canUndo(), "background movement updates queue and undo on UI thread");
    expect(preview.message().contains(QStringLiteral("검사·이동 총 ")), "background movement displays automatic total time");
    QFile moveTiming(QString::fromStdWString((appData / "diagnostics" / "last-move-timing.log").wstring()));
    expect(moveTiming.open(QIODevice::ReadOnly), "background movement timing report exists");
    const auto timingReport = moveTiming.readAll(); moveTiming.close();
    expect(timingReport.contains("operation=move outcome=success") && timingReport.contains("approval_check") &&
        timingReport.contains("executor_plan_check"), "timing includes approval and executor checks");
    if (taggedCrossVolume) for (const auto* stage : {"protected_copy_open", "copy_stream", "copy_flush", "copy_readback",
        "destination_commit", "removal_full_check", "source_delete_and_confirm"})
        expect(timingReport.contains(stage), "timing includes every actual cross-volume transfer phase");
    for (const auto& source : sources) expect(!timingReport.contains(source.toUtf8()), "diagnostic report omits source paths");
    for (const auto& source : sources) expect(!QFile::exists(source), "background source moved");
    preview.undoLastMove(); expect(runner.busy(), "last undo must dispatch to background");
    expect(waitForBackground(runner) && !preview.canUndo(), "background last undo completes");
    if (taggedCrossVolume) { QFile marker(ownedZone); expect(marker.open(QIODevice::ReadOnly) && marker.readAll() == ownedMarker, "cross-volume move and undo preserve exact download marker"); }
    for (const auto& source : sources) { expect(QFile::exists(source), "background original restored"); pending.addUrl(QUrl::fromLocalFile(source)); }
    preview.createPreview(home); preview.prepareSort();
    expect(waitForBackground(runner) && preview.awaitingConfirmation(), "second background confirmation"); preview.confirmSort();
    expect(waitForBackground(runner) && preview.canUndo(), "second background move");
    history.refreshHistory();
    if (history.historyEntries().isEmpty()) { expect(false, "background recent history"); return; }
    history.selectHistoryPlan(history.historyEntries().front().toMap().value("planId").toString());
    history.undoSelectedHistory(); expect(runner.busy(), "history undo must dispatch to background");
    expect(waitForBackground(runner) && !preview.canUndo(), "history completion refreshes last undo");
    if (taggedCrossVolume) {
        QFile marker(ownedZone); expect(marker.open(QIODevice::ReadOnly) && marker.readAll() == ownedMarker, "history undo preserves download marker"); marker.close();
        expect(QFile::remove(ownedZone), "remove only owned fixture marker before legacy recovery regression");
    }
    for (const auto& source : sources) pending.addUrl(QUrl::fromLocalFile(source));
    preview.createPreview(home); preview.freezePreview(); preview.approvePlan();
    const auto plans = workflow.history.incompletePlans();
    if (!plans || plans.value().size() != 1) { expect(false, "background interruption plan"); return; }
    const auto plan = plans.value()[0];
    // 새 고유 시험 계획만 미시작 중단 상태로 만들어 명시적 재개를 검사한다.
    expect(workflow.history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(), "owned background interruption");
    recovery.refreshRecoveryCases();
    recovery.requestRecoveryChoice(QString::fromStdString(plan.id), "resume");
    expect(recovery.awaitingRecoveryConfirmation(), "background recovery explicit confirmation");
    recovery.confirmRecoveryChoice(); expect(runner.busy(), "recovery must dispatch to background");
    recovery.confirmRecoveryChoice();
    expect(waitForBackground(runner) && workflow.history.loadPlan(plan.id).value().status == nookbit::PlanStatus::Completed,
        "background recovery finishes without sharing GUI SQLite connection");
    expect(operations.rollback(plan.id).hasValue(), "owned background fixture final restoration");
#else
    (void)sourceRoot; (void)targetRoot;
#endif
}

// 확인창 이후 파일 변경이 비동기 승인에서 거부되는지 검사하는 함수.
void testBackgroundApprovalSafety() {
#ifdef _WIN32
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "background approval safety directory"); return; }
    const auto rules = projectPath("resources/rules/default.v3.json"), schema = projectPath("schemas/rules-tree-v3.schema.json");
    const auto home = directory.filePath("organized");
    const auto appData = std::filesystem::path(directory.filePath("app-data").toStdWString());
    PreviewWorkflow workflow(rules, schema, appData, true);
    nookbit::UserSettings settings; settings.homeRoot = std::filesystem::path(home.toStdWString());
    nookbit::RuntimeSettingsRepository current(settings);
    nookbit::ThreadLocalOperations operations(directory.filePath("history.sqlite"),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"), rules, schema, {}, appData, current);
    nookbit::BackgroundTaskRunner runner;
    nookbit::PendingFilesModel pending;
    nookbit::PreviewController preview(pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, operations, rules, schema, home);
    preview.setTaskRunner(&runner); preview.setSortWorkflow(&operations);
    const auto source = directory.filePath("changed.jpg");
    QFile file(source); expect(file.open(QIODevice::WriteOnly) && file.write("owned source") > 0, "background changed source"); file.close();
    const auto successfulSource = directory.filePath("successful.jpg");
    QFile successful(successfulSource); expect(successful.open(QIODevice::WriteOnly) && successful.write("owned successful source") > 0, "background successful subset source"); successful.close();
    pending.addUrl(QUrl::fromLocalFile(successfulSource));
    pending.addUrl(QUrl::fromLocalFile(source)); preview.createPreview(home); preview.prepareSort();
    expect(waitForBackground(runner) && preview.awaitingConfirmation(), "background approval safety confirmation");
    expect(file.open(QIODevice::Append) && file.write(" changed") > 0, "change after background confirmation"); file.close();
    preview.confirmSort(); expect(runner.busy(), "approval revalidation must not block screen");
    expect(waitForBackground(runner) && QFile::exists(source) && !QFile::exists(successfulSource) && pending.count() == 1 && preview.canUndo(),
        "partial metadata failure must retain failed pending file and remove only successful file");
    const auto plans = workflow.history.incompletePlans();
    expect(plans && plans.value().size() == 1 && plans.value()[0].status == nookbit::PlanStatus::Failed &&
        std::any_of(plans.value()[0].steps.begin(), plans.value()[0].steps.end(), [](const auto& step) { return step.failure && step.failure->code == "move.source_changed"; }),
        "metadata execution must record changed source as per-file failure");
    preview.undoLastMove(); expect(waitForBackground(runner) && QFile::exists(successfulSource) && QFile::exists(source), "partial metadata UI undo must restore only successful subset");
#endif
}

// 새 고유 두 볼륨 자료만 사용하여 백그라운드 화면 연결을 시험하는 함수.
int runBackgroundCrossTests(const QStringList& args) {
    if (args.size() != 6 || args[2] != "--source-parent" || args[4] != "--destination-parent") return 2;
    for (const auto& parent : {args[3], args[5]}) if (!QFileInfo(parent).isAbsolute() || !QFileInfo(parent).isDir()) return 2;
    nookbit::WindowsStagedCopy volumes;
    const auto a = volumes.inspectVolume(std::filesystem::path(args[3].toStdWString())), b = volumes.inspectVolume(std::filesystem::path(args[5].toStdWString()));
    if (!a || !b || !a.value().localNtfs || !b.value().localNtfs || a.value().name == b.value().name) return 2;
    QTemporaryDir source(QDir(args[3]).filePath("NookBit-bg-source-XXXXXX")), target(QDir(args[5]).filePath("NookBit-bg-target-XXXXXX"));
    source.setAutoRemove(false); target.setAutoRemove(false);
    if (!source.isValid() || !target.isValid()) return 2;
    std::cout << "Background fixtures: " << source.path().toStdString() << " -> " << target.path().toStdString() << '\n';
    testBackgroundWorkflow(source.path(), target.path());
    std::cout << (failures ? "FAIL" : "PASS") << " background move/last undo/history undo/recovery\nOwned fixtures retained.\n";
    return failures ? 1 : 0;
}

// 감시 제안도 기존 대기 목록·고정·확인·이동·저장 흐름을 따르는지 검사하는 함수.
void testWatchedFilesUseNormalSortWorkflow() {
    QTemporaryDir directory;
    expect(directory.isValid(), "watch integration fixture should exist");
    if (!directory.isValid()) return;
    const auto incoming = directory.filePath(QStringLiteral("incoming"));
    expect(QDir().mkpath(incoming), "watched folder should exist");
    const auto first = QDir(incoming).filePath(QStringLiteral("first.jpg"));
    const auto second = QDir(incoming).filePath(QStringLiteral("second.jpg"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "watched fixture should open");
        if (!file.isOpen()) return;
        file.write("watched bytes");
    }
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(appData);
    nookbit::PendingFilesModel pending;
    nookbit::PreviewController preview(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"), home);
    nookbit::JsonPendingFilesRepository queue(directory.filePath(QStringLiteral("pending-files.json")));
    // 실제 앱처럼 목록 변경 때 전체 경로만 저장하는 함수.
    QObject::connect(&pending, &nookbit::PendingFilesModel::countChanged, &pending, [&] {
        std::vector<std::filesystem::path> paths;
        for (const auto& path : pending.filePaths()) paths.emplace_back(path.toStdWString());
        expect(queue.save(paths).hasValue(), "watched queue should persist");
    });
    pending.addUrl(QUrl::fromLocalFile(first));
    preview.createPreview(home);
    expect(preview.hasPreview(), "manual first file should have a draft");
    nookbit::JsonWatchSettingsRepository watchSettings(
        directory.filePath(QStringLiteral("watch.json")), projectPath("schemas/watch.schema.json"));
    nookbit::EvaluateWatchFolderUseCase evaluate(workflow.scanner, workflow.ruleEngine, workflow.rules);
    nookbit::WatchController watch(pending, watchSettings, evaluate, incoming);
    watch.saveSettings(incoming, true, false, 10, false, true, 2, false, 1, false, 1, {});
    watch.checkNow();
    expect(watch.proposalVisible() && pending.count() == 1 && preview.hasPreview(),
           "proposal alone must not alter the queue or authorize movement");
    watch.addProposalToPending();
    expect(pending.count() == 2 && !preview.hasPreview() && !preview.hasApprovedPlan() &&
               QFile::exists(first) && QFile::exists(second),
           "acceptance must deduplicate, invalidate the old draft, and only register paths");
    const auto queued = queue.load();
    expect(queued && queued.value().size() == 2, "accepted watched files should survive restart");
    preview.confirmSort();
    expect(QFile::exists(first) && QFile::exists(second), "watched queue must not bypass confirmation preparation");
    preview.createPreview(home);
    expect(preview.prepareSort() && preview.confirmationSteps().size() == 2 &&
               !preview.hasApprovedPlan() && QFile::exists(first) && QFile::exists(second),
           "watched files must use the same frozen confirmation workflow");
#ifdef _WIN32
    QFile changed(second);
    expect(changed.open(QIODevice::Append), "watched file should reopen after confirmation preparation");
    if (!changed.isOpen()) return;
    changed.write(" edited");
    changed.close();
    preview.confirmSort();
    const auto failedQueue = queue.load();
    expect(!preview.hasPreview() && !preview.hasApprovedPlan() && pending.count() == 2 &&
               QFile::exists(first) && QFile::exists(second) && failedQueue && failedQueue.value().size() == 2,
           "failed watched confirmation must preserve both queue entries on disk and both source files");
    preview.createPreview(home);
    expect(preview.prepareSort(), "changed watched file needs a fresh preview and confirmation");
    preview.confirmSort();
    expect(!QFile::exists(first) && !QFile::exists(second) && pending.count() == 0 && preview.canUndo(),
           "explicit confirmation should move watched files and remove their queue entries");
    const auto afterMove = queue.load();
    expect(afterMove && afterMove.value().empty(), "moved watched files must not return in the saved queue");
    preview.undoLastMove();
    expect(QFile::exists(first) && QFile::exists(second), "watched sort must remain undoable");
#endif
}

// 편집 초안은 정리에 영향이 없고 전체 저장한 규칙만 다음 미리보기에 적용되는지 검사하는 함수.
void testSavedEditorRulesReachMainPreview() {
    QTemporaryDir directory;
    expect(directory.isValid(), "rule-to-preview directory should be available");
    if (!directory.isValid()) return;

    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "rule-to-preview source should open");
    if (!file.isOpen()) return;
    file.write("preview only");
    file.close();

    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "default v3 rules should load");
    if (!initial) return;

    const auto activePath = directory.filePath(QStringLiteral("rules.json"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow workflow(activePath, schema, appData);
    expect(workflow.rules.save(initial.value()).hasValue(), "active v3 rules should initialize");
    nookbit::RuleEditorController editor(workflow.rules, workflow.rules);
    nookbit::PendingFilesModel pending;
    nookbit::PreviewController preview(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations, activePath, schema, home);
    QObject::connect(&editor, &nookbit::RuleEditorController::rulesSaved,
                     &preview, &nookbit::PreviewController::refreshActiveRules);
    pending.addUrl(QUrl::fromLocalFile(source));

    const auto originalLabel = preview.activeRuleLabel();
    expect(originalLabel.contains(QStringLiteral("default")),
           "main page should identify the active saved rule set");
    expect(editor.edit(QStringLiteral("0"), QStringLiteral("images"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("jpg"),
                       QStringLiteral("MiB"), QStringLiteral("Pictures/Edited")),
           "editor draft should change the image destination");
    preview.createPreview(home);
    expect(preview.steps().size() == 1, "unsaved draft should still make an active-rule preview");
    if (preview.steps().size() == 1) {
        expect(!preview.steps().front().toMap().value(QStringLiteral("destination")).toString()
                    .contains(QStringLiteral("Pictures/Edited")),
               "unsaved draft must not affect the main preview");
    }

    expect(editor.save(), "editor should save the new active rules");
    expect(!preview.hasPreview(), "saving rules should invalidate the old preview");
    expect(preview.activeRuleLabel() != originalLabel,
           "main page should refresh the applied rule version after save");
    preview.createPreview(home);
    expect(preview.steps().size() == 1, "saved rules should make a new preview");
    if (preview.steps().size() == 1) {
        expect(preview.steps().front().toMap().value(QStringLiteral("destination")).toString()
                    .contains(QStringLiteral("Pictures/Edited")),
               "new preview should use the rule saved by the editor");
    }
    expect(QFile::exists(source), "rule-to-preview test must not move the source");
}

// 기본 페이지가 이동·충돌·미분류·제외를 파일마다 구분해 표시하는지 검사하는 함수.
void testEveryPreviewOutcomeIsVisible() {
    QTemporaryDir directory;
    expect(directory.isValid(), "preview outcome directory should exist");
    if (!directory.isValid()) return;

    const auto home = directory.filePath(QStringLiteral("organized"));
    expect(QDir().mkpath(home + QStringLiteral("/Pictures")),
           "preview outcome destination should exist");
    for (const auto& name : {QStringLiteral("photo.jpg"), QStringLiteral("already.jpg"),
                             QStringLiteral("notes.txt"), QStringLiteral("ignore.tmp")}) {
        QFile source(directory.filePath(name));
        expect(source.open(QIODevice::WriteOnly), "preview outcome source should open");
        if (!source.isOpen()) return;
        source.write("source bytes");
    }
    QFile occupied(home + QStringLiteral("/Pictures/already.jpg"));
    expect(occupied.open(QIODevice::WriteOnly), "collision fixture should open");
    if (!occupied.isOpen()) return;
    occupied.write("existing bytes");
    occupied.close();

    nookbit::RuleSet rules;
    rules.id = "preview-outcomes";
    rules.version = "2026-10-02T00:00:00Z";
    rules.schemaVersion = 3;
    nookbit::RuleNode images;
    images.id = "images";
    images.atomicCondition = nookbit::ExtensionPredicate{{"jpg"}};
    images.destination = nookbit::RuleDestination{L"Pictures", nookbit::CollisionPolicy::Skip};
    nookbit::RuleNode excluded;
    excluded.id = "ignore-temp";
    excluded.atomicCondition = nookbit::ExtensionPredicate{{"tmp"}};
    excluded.exclude = true;
    rules.nodes = {images, excluded};

    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(rulesPath, schema, appData);
    expect(workflow.rules.save(rules).hasValue(), "preview outcome rules should save");
    nookbit::PendingFilesModel pending;
    for (const auto& name : {QStringLiteral("photo.jpg"), QStringLiteral("already.jpg"),
                             QStringLiteral("notes.txt"), QStringLiteral("ignore.tmp")}) {
        pending.addUrl(QUrl::fromLocalFile(directory.filePath(name)));
    }
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations, rulesPath, schema, home);
    controller.createPreview(home);
    expect(controller.steps().size() == 2 && controller.outcomes().size() == 4,
           "all four decisions should appear without adding non-moves to executable steps");
    QHash<QString, QVariantMap> rows;
    for (const auto& value : controller.outcomes()) {
        const auto row = value.toMap();
        rows.insert(row.value(QStringLiteral("fileName")).toString(), row);
    }
    expect(rows.value(QStringLiteral("photo.jpg")).value(QStringLiteral("statusKey")) == "planned" &&
               !rows.value(QStringLiteral("photo.jpg")).value(QStringLiteral("destination")).toString().isEmpty(),
           "matched file should show its proposed destination");
    expect(rows.value(QStringLiteral("already.jpg")).value(QStringLiteral("statusKey")) == "skipped" &&
               rows.value(QStringLiteral("already.jpg")).value(QStringLiteral("reason")).toString().contains(
                   QStringLiteral("같은 이름")),
           "colliding file should explain why it stays put");
    expect(rows.value(QStringLiteral("notes.txt")).value(QStringLiteral("statusKey")) == "unmatched" &&
               rows.value(QStringLiteral("notes.txt")).value(QStringLiteral("destination")).toString().isEmpty(),
           "unmatched file should show no destination");
    expect(rows.value(QStringLiteral("ignore.tmp")).value(QStringLiteral("statusKey")) == "excluded" &&
               rows.value(QStringLiteral("ignore.tmp")).value(QStringLiteral("rule")) == "ignore-temp",
           "explicitly excluded file should show its matching rule");
    expect(controller.warnings().isEmpty(),
           "expected unmatched and excluded results should not repeat as generic warnings");
    expect(QFile::exists(directory.filePath(QStringLiteral("photo.jpg"))) &&
               QFile::exists(directory.filePath(QStringLiteral("already.jpg"))),
           "preview outcome display must not move files");
}

// 공통 충돌 정책이 승인·실행까지 전달되고 규칙의 개별 설정이 우선하는지 검사하는 함수.
void testCommonCollisionPolicyWorkflow() {
    QTemporaryDir directory;
    expect(directory.isValid(), "collision policy directory should exist");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    const auto home = directory.filePath(QStringLiteral("organized"));
    expect(QDir().mkpath(home + QStringLiteral("/Pictures")), "collision target should exist");
    const auto occupiedPath = home + QStringLiteral("/Pictures/photo.jpg");
    for (const auto& path : {source, occupiedPath}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "collision fixture should open");
        if (!file.isOpen()) return;
        file.write(path == source ? "source bytes" : "existing bytes");
    }

    nookbit::RuleSet rules;
    rules.id = "collision-inheritance";
    rules.version = "2026-10-02T00:00:00Z";
    rules.schemaVersion = 3;
    nookbit::RuleNode images;
    images.id = "images";
    images.atomicCondition = nookbit::ExtensionPredicate{{"jpg"}};
    images.destination = nookbit::RuleDestination{L"Pictures", std::nullopt};
    rules.nodes = {images};
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(rulesPath, schema, appData);
    expect(workflow.rules.save(rules).hasValue(), "inherited collision rules should save");
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations, rulesPath, schema, home);

    controller.createPreview(home);
    const auto renamedPath = home + QStringLiteral("/Pictures/photo (1).jpg");
    expect(controller.steps().size() == 1 &&
               QDir::fromNativeSeparators(controller.steps().front().toMap().value(
                   QStringLiteral("destination")).toString()) == renamedPath,
           "default rename policy should propose a free filename");
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "rename preview should be approvable");

    controller.setCollisionPolicy(QStringLiteral("skip"));
    expect(!controller.hasPreview() && !controller.hasFrozenPlan() && !controller.hasApprovedPlan(),
           "changing policy should invalidate the old draft and approval");
    controller.createPreview(home);
    expect(controller.hasPreview() && !controller.hasMovableSteps() &&
               controller.outcomes().size() == 1 &&
               controller.outcomes().front().toMap().value(QStringLiteral("statusKey")).toString() ==
                   QStringLiteral("skipped"),
           "inherited skip policy should keep a colliding file in place");

    controller.setCollisionPolicy(QStringLiteral("abort"));
    controller.createPreview(home);
    expect(!controller.hasPreview() && controller.message().contains(QStringLiteral("충돌")) &&
               QFile::exists(source),
           "inherited abort policy should stop before any file movement");

    rules.nodes.front().destination->collisionPolicy = nookbit::CollisionPolicy::Rename;
    expect(workflow.rules.save(rules).hasValue(), "explicit collision policy should save");
    controller.refreshActiveRules();
    controller.createPreview(home);
    expect(controller.hasMovableSteps(), "rule-specific rename should override common abort");
    expect(controller.prepareSort(), "sort action should prepare explicit confirmation");
    expect(controller.hasFrozenPlan() && !controller.hasApprovedPlan() && QFile::exists(source),
           "preparing confirmation must freeze without approving or moving");
    expect(controller.hasFrozenPlan() && controller.verifySavedPlan(),
           "selected common policy should remain consistent through freeze and verification");
#ifdef _WIN32
    controller.confirmSort();
    expect(!QFile::exists(source) && QFile::exists(renamedPath) && QFile::exists(occupiedPath),
           "execution should preserve the occupied file and use the rule-specific renamed destination");
    QFile occupied(occupiedPath);
    expect(occupied.open(QIODevice::ReadOnly) && occupied.readAll() == "existing bytes",
           "collision handling must preserve existing destination contents");
    occupied.close();
    controller.undoLastMove();
    expect(QFile::exists(source) && !QFile::exists(renamedPath), "policy workflow should remain undoable");
#endif
}

// 최종 확인 취소·변경 감지·중복 확인이 파일을 잘못 이동시키지 않는지 검사하는 함수.
void testSortConfirmationSafety() {
    QTemporaryDir directory;
    expect(directory.isValid(), "confirmation directory should exist");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "confirmation source should open");
    if (!file.isOpen()) return;
    file.write("before");
    file.close();
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(appData);
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"), home);
    controller.confirmSort();
    expect(QFile::exists(source) && !controller.hasApprovedPlan(),
           "confirmation without preparation must do nothing");
    pending.restorePaths({source});
    expect(!controller.hasPreview() && !controller.hasFrozenPlan() && !controller.hasApprovedPlan(),
           "restored queue must not restore any preview or authorization");
    controller.createPreview(home);
    expect(controller.prepareSort() && controller.awaitingConfirmation(),
           "valid preview should open confirmation");
    expect(controller.confirmationSteps().size() == 1 &&
               QDir::fromNativeSeparators(controller.confirmationSteps().front().toMap().value(
                   QStringLiteral("source")).toString()) == QDir::fromNativeSeparators(source),
           "confirmation should show the frozen source");
    controller.cancelSort();
    controller.confirmSort();
    expect(!controller.awaitingConfirmation() && !controller.hasApprovedPlan() && QFile::exists(source),
           "cancelled confirmation cannot authorize movement");
    expect(controller.prepareSort(), "cancelled frozen plan may be rechecked and offered again");
    expect(file.open(QIODevice::Append), "confirmation source should reopen");
    if (!file.isOpen()) return;
    file.write(" changed");
    file.close();
    controller.confirmSort();
    expect(!controller.hasPreview() && !controller.awaitingConfirmation() &&
               !controller.hasApprovedPlan() && QFile::exists(source) && !QFile::exists(home),
           "changed source must invalidate confirmation without moving");
    controller.createPreview(home);
    expect(controller.prepareSort(), "fresh preview should prepare again");
    controller.setCollisionPolicy(QStringLiteral("skip"));
    controller.confirmSort();
    expect(!controller.awaitingConfirmation() && QFile::exists(source),
           "setting changes must invalidate pending confirmation");
    controller.createPreview(home);
    expect(controller.prepareSort(), "new policy preview should prepare");
    pending.removeAt(0);
    controller.confirmSort();
    expect(!controller.awaitingConfirmation() && QFile::exists(source),
           "pending list changes must invalidate confirmation");
    pending.restorePaths({source, directory.filePath(QStringLiteral("missing.jpg"))});
    controller.createPreview(home);
    expect(pending.count() == 2 && !controller.hasPreview() &&
               controller.message().contains(QStringLiteral("찾을 수 없는 파일")),
           "missing restored files must remain visible and block preview until resolved");
}

// 대기 파일이 미리보기로 이어지고 원본은 그대로 남는지 검사하는 함수.
void testPreviewFromPendingFiles() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;

    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "source fixture should open");
    if (!file.isOpen()) return;
    file.write("original content");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = directory.filePath(QStringLiteral("app-data"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(appData.toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    QString acceptedDestination;
    QObject::connect(&controller, &nookbit::PreviewController::destinationRootAccepted,
                     &controller, [&acceptedDestination](const QString& root) {
        acceptedDestination = root;
    });
    controller.createPreview(home);

    expect(controller.hasPreview(), "preview should be available");
    expect(acceptedDestination == home,
           "successful preview should offer its destination as the next default");
    expect(controller.steps().size() == 1, "image should create one planned step");
    if (controller.steps().size() == 1) {
        const auto step = controller.steps().at(0).toMap();
        expect(step.value(QStringLiteral("rule")).toString() == QStringLiteral("images"),
               "image rule should be displayed");
        expect(step.value(QStringLiteral("destination")).toString().startsWith(home),
               "destination should stay under selected home");
        expect(step.value(QStringLiteral("destination")).toString().contains(
                   QStringLiteral("Pictures")),
               "destination should include the image category");
    }
    expect(QFile::exists(source), "preview must not move the original file");
    expect(!QFile::exists(home), "preview must not create the destination directory");

    controller.freezePreview();
    expect(controller.hasFrozenPlan(), "unchanged preview should be frozen");
    expect(QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "frozen plan should be saved to SQLite");
    nookbit::SqliteHistoryRepository stored(
        directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    const auto plans = stored.incompletePlans();
    expect(plans.hasValue() && plans.value().size() == 1,
           "stored plan should be readable after saving");
    if (plans && plans.value().size() == 1) {
        const auto& storedPlan = plans.value().front();
        expect(plans.value().front().status == nookbit::PlanStatus::Planned,
               "stored plan must be Planned, not Approved");
        expect(plans.value().front().planHash.size() == 64,
               "stored plan should contain a SHA-256 digest");
        expect(plans.value().front().steps.size() == 1 &&
                   plans.value().front().steps.front().source ==
                       std::filesystem::path(source.toStdWString()),
               "stored step should preserve the selected source");
        nookbit::QtPlanHasher hasher;
        const auto expectedHash = hasher.sha256(
            nookbit::PlanSignature::approvalPayload(storedPlan));
        expect(storedPlan.planHash == expectedHash,
               "stored plan should round-trip without changing its approval hash");
    }
    expect(controller.verifySavedPlan(), "unchanged saved plan should pass revalidation");
    expect(QFile::exists(source) && !QFile::exists(home),
           "saving a plan must not move files or create the destination");

    pending.removeAt(0);
    expect(!controller.hasPreview() && controller.steps().isEmpty(),
           "changing the pending list should clear a stale preview");

    pending.addUrl(QUrl::fromLocalFile(source));
    controller.createPreview(QStringLiteral("relative/path"));
    expect(!controller.hasPreview() && !controller.message().isEmpty(),
           "relative destination should be rejected");
    expect(QFile::exists(source), "invalid preview must not change the original file");
}

// 트리 JSON의 리프 결과가 기존 미리보기·계획 저장 흐름에 연결되는지 검사하는 함수.
void testTreeRulesReachPreview() {
    QTemporaryDir directory;
    expect(directory.isValid(), "tree-preview directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "tree-preview source should open");
    if (!file.isOpen()) return;
    file.write("small photo");
    file.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    const auto rulesPath = projectPath("resources/rules/tree.example.json");
    const auto schemaPath = projectPath("schemas/rules-tree.schema.json");
    PreviewWorkflow workflow(rulesPath, schemaPath, appData);
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations, rulesPath, schemaPath, home);
    controller.createPreview(home);
    expect(controller.hasPreview() && controller.steps().size() == 1,
           "tree rules should produce one preview step");
    if (controller.steps().size() != 1) {
        return;
    }
    const auto step = controller.steps().front().toMap();
    expect(step.value(QStringLiteral("rule")).toString() == QStringLiteral("other-images") &&
               QDir::fromNativeSeparators(step.value(QStringLiteral("destination")).toString()).contains(
                   QStringLiteral("Pictures/Other")),
           "tree else leaf should supply the preview destination");
    controller.freezePreview();
    expect(controller.hasFrozenPlan() && controller.verifySavedPlan(),
           "tree plan should remain valid after saving and rechecking");
    expect(QFile::exists(source), "tree preview must not move the source");
}

// 저장 뒤 원본이 바뀌면 재검사를 통과하지 못하는지 검사하는 함수.
void testChangedFileAfterSaveFailsVerification() {
    QTemporaryDir directory;
    expect(directory.isValid(), "post-save directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "post-save source should open");
    if (!file.isOpen()) return;
    file.write("before");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    controller.createPreview(home);
    controller.freezePreview();
    expect(controller.hasFrozenPlan(), "post-save plan should be stored");
    expect(file.open(QIODevice::Append), "post-save source should reopen");
    if (!file.isOpen()) return;
    file.write(" changed");
    file.close();
    expect(!controller.verifySavedPlan(), "changed source must fail saved-plan verification");
    expect(QFile::exists(source) && !QFile::exists(home),
           "failed verification must not move or copy files");
}

// 저장된 단계 경로가 DB에서 바뀌면 계획 해시 검사로 거절하는지 확인하는 함수.
void testTamperedSavedPlanFailsVerification() {
    QTemporaryDir directory;
    expect(directory.isValid(), "tamper-test directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "tamper-test source should open");
    if (!file.isOpen()) return;
    file.write("image");
    file.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto dbPath = directory.filePath(QStringLiteral("history.sqlite"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    controller.createPreview(home);
    controller.freezePreview();
    expect(controller.verifySavedPlan(), "untampered plan should pass first");

    const QString connection = QStringLiteral("nookbit-tamper-test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(dbPath);
        expect(database.open(), "tamper-test database should open");
        if (database.isOpen()) {
            QSqlQuery query(database);
            expect(query.exec(QStringLiteral(
                       "UPDATE steps SET destination_path = 'C:/unexpected/photo.jpg'")),
                   "tamper-test destination should be modified");
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    expect(!controller.verifySavedPlan(), "modified DB step must fail its saved hash");
    expect(QFile::exists(source) && !QFile::exists(home),
           "tamper detection must not change source or destination");
}

// 같은 버전의 규칙 내용이 바뀌어도 오래된 미리보기를 거절하는지 검사하는 함수.
void testChangedRulesCannotBeFrozen() {
    QTemporaryDir directory;
    expect(directory.isValid(), "rule-change directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "rule-change source should open");
    if (!file.isOpen()) return;
    file.write("image");
    file.close();

    QFile example(projectPath("resources/rules/default.example.json"));
    expect(example.open(QIODevice::ReadOnly), "example rules should open");
    if (!example.isOpen()) return;
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    QFile rulesFile(rulesPath);
    expect(rulesFile.open(QIODevice::WriteOnly), "rule-change copy should open");
    if (!rulesFile.isOpen()) return;
    rulesFile.write(example.readAll());
    rulesFile.close();
    example.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(rulesPath, projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        rulesPath, projectPath("schemas/rules.schema.json"),
        home);
    controller.createPreview(home);
    expect(controller.hasPreview(), "rule-change preview should be available");

    expect(rulesFile.open(QIODevice::ReadOnly), "rule-change copy should reopen");
    if (!rulesFile.isOpen()) return;
    auto object = QJsonDocument::fromJson(rulesFile.readAll()).object();
    rulesFile.close();
    auto rules = object.value(QStringLiteral("rules")).toArray();
    auto first = rules.at(0).toObject();
    auto then = first.value(QStringLiteral("then")).toObject();
    then.insert(QStringLiteral("destination"), QStringLiteral("Other/{yyyy}/{MM}"));
    first.insert(QStringLiteral("then"), then);
    rules.replace(0, first);
    object.insert(QStringLiteral("rules"), rules);
    expect(rulesFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "changed rules should be writable");
    if (!rulesFile.isOpen()) return;
    rulesFile.write(QJsonDocument(object).toJson());
    rulesFile.close();

    controller.freezePreview();
    expect(!controller.hasFrozenPlan() && !controller.hasPreview(),
           "same-version rule edits should invalidate the preview");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "changed rules must not create a stored plan");
    expect(QFile::exists(source), "rule-change rejection must leave the source in place");
}

// 분류 결과가 같아도 설정 자체가 바뀌면 고정을 거절하는지 검사하는 함수.
void testChangedSettingsCannotBeFrozen() {
    QTemporaryDir directory;
    expect(directory.isValid(), "settings-change directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "settings-change source should open");
    if (!file.isOpen()) return;
    file.write("image");
    file.close();

    MutableSettingsRepository settings;
    settings.settings.homeRoot = std::filesystem::path(
        directory.filePath(QStringLiteral("organized")).toStdWString());
    MemoryRulesRepository rules;
    nookbit::ClassificationRule rule;
    rule.id = "images";
    rule.enabled = true;
    rule.priority = 100;
    rule.extensions = {"jpg"};
    rule.destinationRelativePath = L"Pictures";
    rules.rules = {"default", "rules-1", {rule}};

    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, std::filesystem::path(
        directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::QtPlanHasher hasher;
    nookbit::SqliteHistoryRepository history(
        directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher);
    const auto sourcePath = std::filesystem::path(source.toStdWString());
    const auto draft = preview.execute({"rules-1", {sourcePath}});
    expect(draft.hasValue(), "settings-change draft should be created");
    if (!draft) return;

    settings.settings.recursiveScan = true;
    nookbit::FreezePreviewUseCase freeze(rules, scanner, planner, hasher, history);
    const auto result = freeze.execute(draft.value(), {sourcePath}, settings.settings);
    expect(!result.hasValue() && result.error().code == "freeze.settings_changed",
           "setting changes must be rejected even when planned destination is unchanged");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "changed settings must not create a stored plan");
    expect(QFile::exists(source), "settings-change rejection must leave the source in place");

    settings.settings.recursiveScan = false;
    const auto frozen = freeze.execute(draft.value(), {sourcePath}, settings.settings);
    expect(frozen.hasValue(), "restored settings should allow storing the plan");
    if (!frozen) return;
    nookbit::VerifySavedPlanUseCase verify(rules, scanner, planner, hasher, history);
    expect(verify.execute(frozen.value().id, {sourcePath}, settings.settings).hasValue(),
           "unchanged settings and rules should verify");

    settings.settings.recursiveScan = true;
    const auto changedSettings = verify.execute(
        frozen.value().id, {sourcePath}, settings.settings);
    expect(!changedSettings.hasValue() && changedSettings.error().code == "verify.settings_changed",
           "post-save setting changes should fail verification");
    settings.settings.recursiveScan = false;
    rules.rules.rules.front().destinationRelativePath = L"Other";
    const auto changedRules = verify.execute(
        frozen.value().id, {sourcePath}, settings.settings);
    expect(!changedRules.hasValue() && changedRules.error().code == "verify.rules_changed",
           "post-save rule changes should fail verification even with the same version");
}

// 미리보기 뒤 원본 파일이 바뀌면 저장을 거부하는지 검사하는 함수.
void testChangedFileCannotBeFrozen() {
    QTemporaryDir directory;
    expect(directory.isValid(), "stale-test directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "stale-test source should open");
    if (!file.isOpen()) return;
    file.write("before");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    controller.createPreview(home);
    expect(controller.hasPreview(), "stale-test preview should be available");
    expect(file.open(QIODevice::Append), "stale-test source should reopen");
    if (!file.isOpen()) return;
    file.write(" changed");
    file.close();

    controller.freezePreview();
    expect(!controller.hasFrozenPlan() && !controller.hasPreview(),
           "modified file should invalidate the preview");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "stale preview must not create a saved plan");
    expect(QFile::exists(source), "stale rejection must leave the source in place");
}

// 화면에서 명시적 승인 뒤 파일 하나를 이동하고 원래 위치로 되돌리는 흐름.
void testSingleFileApproveMoveUndo() {
#ifdef _WIN32
    QTemporaryDir directory;
    expect(directory.isValid(), "move-flow directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "move-flow source should open");
    if (!file.isOpen()) return;
    file.write("keep this content");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    controller.createPreview(home);
    expect(controller.hasPreview() && controller.steps().size() == 1,
           "single file should create one preview step");
    if (controller.steps().size() != 1) return;
    const auto destination = controller.steps().front().toMap()
                                 .value(QStringLiteral("destination")).toString();
    controller.moveApprovedFile();
    expect(QFile::exists(source) && !QFile::exists(destination),
           "move without approval must leave the file untouched");
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "explicit approval should be recorded");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "approval alone must not move the file");
    controller.moveApprovedFile();
    expect(!QFile::exists(source) && QFile::exists(destination),
           "approved file should move to its planned destination");
    expect(pending.count() == 0 && controller.canUndo() && !controller.hasApprovedPlan(),
           "completed move should leave an undo action and clear the pending item");
    controller.undoLastMove();
    expect(QFile::exists(source) && !QFile::exists(destination) && !controller.canUndo(),
           "undo should restore the original file without leaving an active undo action");
    QFile restored(source);
    expect(restored.open(QIODevice::ReadOnly) && restored.readAll() == "keep this content",
           "undo should preserve the file contents");
#endif
}

// 여러 대기 파일을 한 계획으로 승인하고 되돌릴 수 있는지 검사하는 함수.
void testMultipleFilesCanBeMoved() {
    QTemporaryDir directory;
    expect(directory.isValid(), "multi-file directory should be available");
    if (!directory.isValid()) return;
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    const auto unmatched = directory.filePath(QStringLiteral("notes.txt"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "multi-file source should open");
        if (!file.isOpen()) return;
        file.write("content");
    }
    QFile noteFile(unmatched);
    expect(noteFile.open(QIODevice::WriteOnly) && noteFile.write("keep") == 4,
           "unmatched file should be written");
    noteFile.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(first));
    pending.addUrl(QUrl::fromLocalFile(second));
    pending.addUrl(QUrl::fromLocalFile(unmatched));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    controller.createPreview(home);
    const auto previewRows = controller.steps();
    expect(previewRows.size() == 2, "both classified files should appear in preview");
    if (previewRows.size() != 2) return;
    const auto firstDestination = previewRows.at(0).toMap().value(QStringLiteral("destination")).toString();
    const auto secondDestination = previewRows.at(1).toMap().value(QStringLiteral("destination")).toString();
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "multi-file plan should be approved from the screen");
    expect(QFile::exists(first) && QFile::exists(second),
           "multi-file approval must not move either file");
    controller.moveApprovedFile();
    expect(!QFile::exists(first) && !QFile::exists(second) &&
               QFile::exists(firstDestination) && QFile::exists(secondDestination) &&
               QFile::exists(unmatched) && pending.count() == 1 && controller.canUndo(),
           "approved multi-file plan should move only classified files and offer undo");
    controller.undoLastMove();
    expect(QFile::exists(first) && QFile::exists(second) &&
               !QFile::exists(firstDestination) && !QFile::exists(secondDestination) &&
               QFile::exists(unmatched) && pending.count() == 1 && !controller.canUndo(),
           "multi-file undo should restore both files");
}

// 기록에서 마지막 이동을 되돌리면 계획 화면의 되돌리기 버튼도 꺼지는지 검사하는 함수.
void testHistoryUndoUpdatesLastMove() {
#ifdef _WIN32
    QTemporaryDir directory;
    expect(directory.isValid(), "shared-undo directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "shared-undo source should open");
    if (!file.isOpen()) return;
    file.write("original bytes");
    file.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(appData);
    nookbit::PreviewController preview(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, workflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"), home);
    nookbit::HistoryController history(workflow.history, workflow.operations);
    QObject::connect(&history, &nookbit::HistoryController::rollbackAttempted,
                     &preview, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    preview.createPreview(home);
    preview.freezePreview();
    preview.approvePlan();
    preview.moveApprovedFile();
    expect(preview.canUndo(), "last move should offer undo before history rollback");
    history.refreshHistory();
    if (history.historyEntries().isEmpty()) return;
    history.selectHistoryPlan(history.historyEntries().front().toMap()
                                  .value(QStringLiteral("planId")).toString());
    history.undoSelectedHistory();
    expect(QFile::exists(source) && !preview.canUndo(),
           "history rollback should restore file and clear last-move undo");
#endif
}

// 앱을 다시 연 뒤 과거 작업을 골라 안전하게 되돌리는 흐름을 검사하는 함수.
void testHistoryUndoAfterRestart() {
#ifdef _WIN32
    QTemporaryDir directory;
    expect(directory.isValid(), "history directory should be available");
    if (!directory.isValid()) return;
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly) && file.write("original bytes") == 14,
               "history source should be written");
    }
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto dbPath = directory.filePath(QStringLiteral("history.sqlite"));
    const auto rulesPath = projectPath("resources/rules/default.example.json");
    const auto schemaPath = projectPath("schemas/rules.schema.json");
    const auto migrationPath = projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    QString firstPlanId;
    QString firstDestination;
    QString secondDestination;
    {
        nookbit::PendingFilesModel pending;
        PreviewWorkflow previewWorkflow(rulesPath, schemaPath, appData);
        nookbit::PreviewController controller(
            pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
            previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
            previewWorkflow.history, previewWorkflow.operations,
            rulesPath, schemaPath, home);
        nookbit::HistoryController historyController(previewWorkflow.history, previewWorkflow.operations);
        QObject::connect(&historyController, &nookbit::HistoryController::rollbackAttempted,
                         &controller, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
        pending.addUrl(QUrl::fromLocalFile(first));
        controller.createPreview(home);
        firstDestination = controller.steps().front().toMap().value(QStringLiteral("destination")).toString();
        controller.freezePreview();
        controller.approvePlan();
        controller.moveApprovedFile();
        historyController.refreshHistory();
        expect(historyController.historyEntries().size() == 1, "first move should appear in history");
        if (historyController.historyEntries().isEmpty()) return;
        firstPlanId = historyController.historyEntries().front().toMap().value(QStringLiteral("planId")).toString();

        pending.addUrl(QUrl::fromLocalFile(second));
        controller.createPreview(home);
        secondDestination = controller.steps().front().toMap().value(QStringLiteral("destination")).toString();
        controller.freezePreview();
        controller.approvePlan();
        controller.moveApprovedFile();
    }
    expect(QFile::exists(firstDestination) && QFile::exists(secondDestination),
           "both files should stay moved after closing the first controller");
    nookbit::PendingFilesModel pendingAfterRestart;
    PreviewWorkflow restartedWorkflow(rulesPath, schemaPath, appData);
    nookbit::PreviewController restarted(
        pendingAfterRestart, restartedWorkflow.useCase, restartedWorkflow.freezeUseCase,
        restartedWorkflow.verifyUseCase, restartedWorkflow.approveUseCase,
        restartedWorkflow.history, restartedWorkflow.operations,
        rulesPath, schemaPath, home);
    nookbit::HistoryController restartedHistory(restartedWorkflow.history, restartedWorkflow.operations);
    QObject::connect(&restartedHistory, &nookbit::HistoryController::rollbackAttempted,
                     &restarted, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    restartedHistory.refreshHistory();
    expect(restartedHistory.historyEntries().size() == 2, "restart should reload both past moves");
    restartedHistory.selectHistoryPlan(firstPlanId);
    expect(restartedHistory.canUndoSelectedHistory() && restartedHistory.selectedHistoryFileCount() == 1,
           "older selected move should offer undo");
    expect(restartedHistory.historySteps().size() == 1,
           "older selected move should show its file");
    if (restartedHistory.historySteps().size() == 1) {
        const auto shownSource = restartedHistory.historySteps().front().toMap()
                                     .value(QStringLiteral("source")).toString();
        expect(QDir::fromNativeSeparators(shownSource) == QDir::fromNativeSeparators(first),
               "older selected move should show the original path");
    }

    QFile occupied(first);
    expect(occupied.open(QIODevice::WriteOnly) && occupied.write("new occupant") == 12,
           "source collision should be created");
    occupied.close();
    restartedHistory.undoSelectedHistory();
    expect(QFile::exists(firstDestination) && QFile::exists(first) &&
               restartedHistory.historyMessage().contains(QStringLiteral("되돌리지 못했어요")),
           "history undo must refuse an occupied original path");
    expect(QFile::remove(first), "collision fixture should be removed");
    restartedHistory.selectHistoryPlan(firstPlanId);
    restartedHistory.undoSelectedHistory();
    expect(QFile::exists(first) && !QFile::exists(firstDestination) &&
               QFile::exists(secondDestination) && !QFile::exists(second),
           "chosen older move should undo without touching the newer move");
    restartedHistory.selectHistoryPlan(firstPlanId);
    expect(!restartedHistory.canUndoSelectedHistory(), "already undone history must not be undoable again");
#endif
}

// 중단 작업이 화면에 나타나고 확인 후 기록만 정리되는지 검사하는 함수.
void testRecoveryCasesInController() {
    QTemporaryDir directory;
    expect(directory.isValid(), "recovery-screen directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile sourceFile(source);
    expect(sourceFile.open(QIODevice::WriteOnly), "recovery-screen source should open");
    if (!sourceFile.isOpen()) return;
    sourceFile.write("original bytes");
    sourceFile.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto databasePath = directory.filePath(QStringLiteral("history.sqlite"));
    const auto migrationPath = projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history, previewWorkflow.operations,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        home);
    nookbit::RecoveryController recoveryController(previewWorkflow.history);
    QObject::connect(&controller, &nookbit::PreviewController::recoveryReconcileRequested,
                     &recoveryController, &nookbit::RecoveryController::reconcileAndRefreshCases);
    controller.createPreview(home);
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "recovery-screen plan should be approved");
    if (!controller.hasApprovedPlan()) return;
    nookbit::SqliteHistoryRepository history(databasePath, migrationPath);
    auto plans = history.incompletePlans();
    expect(plans.hasValue() && plans.value().size() == 1,
           "recovery-screen plan should be readable");
    if (!plans || plans.value().size() != 1) return;
    const auto& plan = plans.value().front();
    const auto& step = plan.steps.front();
    expect(history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(),
           "recovery-screen execution should begin");
    expect(history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(),
           "recovery-screen intent should be recorded");
    const auto destination = QString::fromStdWString(step.destination.wstring());
    expect(QDir().mkpath(QFileInfo(destination).absolutePath()),
           "recovery-screen destination parent should exist");
    expect(QFile::copy(source, destination), "recovery-screen copy should be created");
    nookbit::FilesystemScanner scanner;
    nookbit::QtPlanHasher hasher;
    nookbit::RecoveryService recovery(history, scanner, hasher);
    auto reconciled = recovery.reconcile();
    expect(reconciled.hasValue() && reconciled.value().needsAttention.size() == 1,
           "ambiguous recovery-screen plan should require attention");
    recoveryController.refreshRecoveryCases();
    expect(recoveryController.recoveryCases().size() == 1 &&
               !recoveryController.recoveryCases().front().toMap().value(QStringLiteral("canResolve")).toBool(),
           "screen should show ambiguity without offering confirmation");
    expect(QFile::remove(destination), "test copy should be removed");
    recoveryController.refreshRecoveryCases();
    expect(recoveryController.recoveryCases().size() == 1 &&
               recoveryController.recoveryCases().front().toMap().value(QStringLiteral("canResolve")).toBool(),
           "screen should offer confirmation when only the matching source remains");
    recoveryController.resolveRecoveryCase(QString::fromStdString(plan.id));
    expect(recoveryController.recoveryCases().isEmpty() && QFile::exists(source),
           "screen confirmation should clear the record without moving the source");
}

// 시작 복구가 v2 복원 완료 기록을 보존하고 화면 경고도 유지하는지 검사하는 함수.
void testV2StartupRecoveryNotice() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "v2 startup screen fixture"); return; }
    const auto source = directory.filePath("photo.jpg"), home = directory.filePath("organized");
    QFile original(source); expect(original.open(QIODevice::WriteOnly | QIODevice::NewOnly) && original.write("owned startup original") > 0, "v2 startup source"); original.close();
    const auto appData = std::filesystem::path(directory.filePath("app-data").toStdWString());
    PreviewWorkflow workflow(projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), appData, true);
    nookbit::PendingFilesModel pending; pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController controller(pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase, workflow.approveUseCase,
        workflow.history, workflow.operations, projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), home);
    controller.createPreview(home); controller.freezePreview(); controller.approvePlan(); controller.moveApprovedFile();
    const auto entries = workflow.history.recentPlans(1);
    if (!entries || entries.value().empty()) { expect(false, "v2 startup completed plan"); return; }
    const auto loaded = workflow.history.loadPlan(entries.value()[0].id);
    if (!loaded || loaded.value().status != nookbit::PlanStatus::Completed) { expect(false, "v2 startup move completes"); return; }
    const auto plan = loaded.value();
    const auto& step = plan.steps[0];
    expect(workflow.history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::RollingBack}).hasValue() &&
        workflow.history.appendEvent(nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::RollbackIntentRecorded}).hasValue() &&
        nookbit::WindowsFileMover::moveNoReplace(step.destination, step.source).hasValue() &&
        workflow.history.appendEvent(nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::RolledBack}).hasValue(), "v2 startup interrupted final undo record");
    nookbit::RecoveryService startup(workflow.history, workflow.scanner, workflow.hasher, &workflow.journal);
    expect(startup.reconcile().hasValue(), "v2 startup inspection succeeds");
    nookbit::RecoveryController recovery(workflow.history, nullptr, &workflow.journal);
    recovery.setRecoveryNotice("pending startup review"); recovery.refreshRecoveryCases();
    expect(recovery.recoveryCases().size() == 1 && !recovery.recoveryNotice().isEmpty() &&
        !recovery.recoveryCases()[0].toMap().value("canResolve").toBool(), "v2 startup warning must remain visible with legacy resolution disabled");
    recovery.resolveRecoveryCase(QString::fromStdString(plan.id));
    const auto retained = workflow.history.loadPlan(plan.id);
    expect(retained && retained.value().status == nookbit::PlanStatus::RollingBack && retained.value().steps[0].status == nookbit::StepStatus::RolledBack &&
        QFile::exists(source), "old screen action cannot erase v2 restored evidence");
}

// 확인 전·취소·나중에·오래된 승인·중복 확인의 화면 흐름을 검사하는 대역.
class RecoveryChoices final : public nookbit::IInterruptedOperations {
public:
    std::string hash = std::string(64, 'a');
    int executions{0}, mutations{0};
    bool remaining{true}, unavailable{false};
    // 파일·DB 접근 없이 표시 후보만 반환하는 함수.
    nookbit::Result<std::vector<nookbit::InterruptedOperationView>> inspectAll() override {
        if (unavailable) return nookbit::Result<std::vector<nookbit::InterruptedOperationView>>::failure({"test.unavailable", "Owned inspection failed"});
        nookbit::InterruptedOperationView view;
        view.inspection.planId = "owned-plan"; view.inspection.pending = 1; view.inspection.moved = 1;
        view.planHash = hash; view.formatVersion = 2; view.canResume = true; view.canRollback = false;
        view.rollbackBlock = nookbit::Error{"recovery.choice_blocked", "Owned rollback blocker"};
        return nookbit::Result<std::vector<nookbit::InterruptedOperationView>>::success(remaining ? std::vector{view} : std::vector<nookbit::InterruptedOperationView>{});
    }
    // 표시 해시가 바뀌면 실패하고 명시적 실행만 변경으로 세는 함수.
    nookbit::Result<void> execute(const nookbit::RecoveryRequest& request) override {
        ++executions;
        if (request.action == nookbit::RecoveryAction::Later) return nookbit::Result<void>::success();
        if (request.planHash != hash) return nookbit::Result<void>::failure({"recovery.choice_stale", "Owned approval changed"});
        ++mutations; remaining = false; return nookbit::Result<void>::success();
    }
};

// 화면 선택만으로 실행되지 않고 확인·취소·재검사가 안전한지 검사하는 함수.
void testRecoveryConfirmationFlow() {
    QTemporaryDir directory;
    PreviewWorkflow workflow(std::filesystem::path(directory.filePath("app-data").toStdWString()));
    RecoveryChoices operations;
    nookbit::RecoveryController controller(workflow.history, nullptr, &workflow.journal, &operations);
    controller.refreshRecoveryCases();
    expect(controller.recoveryCases().size() == 1 && !controller.recoveryNotice().isEmpty() && operations.executions == 0, "startup recovery only inspects");
    controller.requestRecoveryChoice("owned-plan", "rollback");
    expect(!controller.awaitingRecoveryConfirmation() && operations.executions == 0, "blocked choice cannot open execution confirmation");
    controller.requestRecoveryChoice("owned-plan", "resume");
    expect(controller.awaitingRecoveryConfirmation() && operations.executions == 0, "request opens confirmation without file execution");
    controller.cancelRecoveryChoice(); controller.confirmRecoveryChoice();
    expect(!controller.awaitingRecoveryConfirmation() && operations.executions == 0, "cancel and repeated confirmation do nothing");
    controller.requestRecoveryChoice("owned-plan", "later");
    expect(operations.executions == 1 && operations.mutations == 0 && controller.recoveryCases().size() == 1, "Later retains the unresolved operation without changes");
    controller.requestRecoveryChoice("owned-plan", "resume"); operations.hash = std::string(64, 'b'); controller.confirmRecoveryChoice();
    expect(operations.executions == 2 && operations.mutations == 0 && !controller.awaitingRecoveryConfirmation(), "confirmation retains old hash and rejects stale approval");
    controller.confirmRecoveryChoice();
    expect(operations.executions == 2, "failed confirmation cannot repeat automatically");
    controller.requestRecoveryChoice("owned-plan", "resume"); controller.confirmRecoveryChoice(); controller.confirmRecoveryChoice();
    expect(operations.executions == 3 && operations.mutations == 1 && controller.recoveryCases().isEmpty(), "explicit confirmed recovery completes once");
    operations.remaining = true; controller.refreshRecoveryCases(); controller.requestRecoveryChoice("owned-plan", "resume");
    operations.unavailable = true; controller.refreshRecoveryCases(); controller.confirmRecoveryChoice();
    expect(controller.recoveryCases().isEmpty() && !controller.awaitingRecoveryConfirmation() && operations.executions == 3 &&
        !controller.recoveryNotice().isEmpty(), "failed inspection clears actionable stale cards and confirmation, not the warning");
}

// 시험 전용 DB의 감사 사건 수만 읽는 함수.
qlonglong ownedEventCount(const QString& path) {
    const auto name = QUuid::createUuid().toString();
    qlonglong result = -1;
    { auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(path);
      if (db.open()) { QSqlQuery query(db); if (query.exec("SELECT COUNT(*) FROM events") && query.next()) result = query.value(0).toLongLong(); }
      db.close(); }
    QSqlDatabase::removeDatabase(name); expect(result >= 0, "owned recovery audit count"); return result;
}

// 실제 혼합 중단을 화면의 명시적 확인을 거쳐 재개·복원하는 함수.
void testNativeRecoveryChoice(const QString& sourceRun, const QString& targetRun, int scenario) {
    nookbit::OperationMutex lock; if (!lock.acquire()) { expect(false, "owned UI recovery lock"); return; }
    const int index = 10 + scenario;
    const auto c = QDir(sourceRun).filePath(QString("case-%1/incoming").arg(index));
    const auto d = QDir(targetRun).filePath(QString("case-%1").arg(index));
    expect(QDir().mkpath(c) && QDir().mkpath(QDir(d).filePath("incoming")), "owned interrupted UI directories");
    const QStringList sources{QDir(c).filePath("first.jpg"), QDir(d).filePath("incoming/second.jpg"), QDir(c).filePath("third.jpg")};
    for (int i = 0; i < sources.size(); ++i) {
        QFile file(sources[i]); const auto bytes = i == 2 ? QByteArray(4 * 1024 * 1024 + 37, 'x') : QByteArray("owned UI recovery ") + QByteArray::number(i);
        expect(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size(), "owned interrupted UI originals");
    }
    const auto home = QDir(d).filePath("organized");
    const auto appData = std::filesystem::path(QDir(d).filePath("app-data").toStdWString());
    PreviewWorkflow workflow(projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), appData, true);
    nookbit::PendingFilesModel pending; for (const auto& source : sources) pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController preview(pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase, workflow.approveUseCase,
        workflow.history, workflow.operations, projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), home);
    preview.createPreview(home); preview.freezePreview(); preview.approvePlan();
    const auto entries = workflow.history.incompletePlans();
    if (!entries || entries.value().size() != 1) { expect(false, "owned recovery approved plan"); return; }
    const auto plan = entries.value()[0];
    expect(plan.status == nookbit::PlanStatus::Approved && plan.steps.size() == 3, "owned recovery plan approval and steps");
    if (plan.steps.size() != 3) return;
    expect(workflow.history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(), "owned recovery execution intent");
    for (int i = 0; i < 2; ++i) {
        const auto& step = plan.steps[i];
        expect(workflow.history.appendEvent(nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(), "owned moved prefix intent");
        if (step.movement->strategy == nookbit::MoveStrategy::VerifiedCopy)
            expect(workflow.forward.execute(plan.id, step.id, "owned-prefix", step.destination.parent_path() / "owned-prefix.tmp").hasValue(), "owned moved prefix transfer");
        else expect(nookbit::WindowsFileMover::moveNoReplace(step.source, step.destination).hasValue() &&
            workflow.history.appendEvent(nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::Applied}).hasValue(), "owned moved prefix rename");
    }
    std::optional<nookbit::TransferAttempt> partial;
    if (scenario >= 3) {
        const auto& step = plan.steps[2];
        expect(workflow.history.appendEvent(nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(), "owned last copy intent");
        nookbit::TransferAttempt attempt; attempt.id = "owned-partial"; attempt.planId = plan.id; attempt.stepId = step.id;
        attempt.source = step.source; attempt.destination = step.destination; attempt.staging = step.destination.parent_path() / "owned-partial.tmp";
        attempt.sourceFingerprint = step.fingerprint;
        if (scenario == 5) expect(workflow.journal.begin(attempt).hasValue(), "owned prepared-only transfer");
        else {
            int checks = 0; nookbit::CommitTransferCopyUseCase copy(workflow.copy, workflow.journal);
            expect(!copy.execute(attempt, [&] { return ++checks >= 3; }, &*step.movement) && std::filesystem::file_size(attempt.staging) == 1024 * 1024,
                "owned real 1MiB UI partial copy");
        }
        const auto state = workflow.journal.load(attempt.id); if (state) partial = state.value(); else expect(false, "owned partial evidence");
    }
    nookbit::UserSettings settings; settings.homeRoot = std::filesystem::path(home.toStdWString());
    nookbit::RuntimeSettingsRepository current(settings);
    nookbit::SafeExecutor runner(current, workflow.verifyUseCase, workflow.scanner, workflow.hasher, workflow.history, appData, &workflow.copy, &workflow.forward, &workflow.reverse);
    nookbit::InspectInterruptedPlanUseCase inspector(workflow.history, workflow.journal, workflow.files, workflow.hasher, &workflow.copy);
    nookbit::WindowsTransferRecoveryGuard removalGuard; nookbit::WindowsTransferCommitRecoveryGuard commitGuard;
    nookbit::ResumeInterruptedTransferUseCase resolver(workflow.history, workflow.journal, workflow.copy, workflow.removal, workflow.files, removalGuard, commitGuard, workflow.hasher);
    nookbit::RecoverInterruptedPlanUseCase recover(inspector, workflow.history, current, workflow.rules, workflow.hasher, runner, &resolver);
    nookbit::InterruptedOperationRecoveryService operations(inspector, workflow.history, recover);
    nookbit::RecoveryController controller(workflow.history, nullptr, &workflow.journal, &operations);
    const auto database = QDir(d).filePath("history.sqlite");
    const auto before = workflow.history.loadPlan(plan.id); const auto events = ownedEventCount(database);
    controller.refreshRecoveryCases();
    expect(controller.recoveryCases().size() == 1 && ownedEventCount(database) == events && before &&
        nookbit::PlanSignature::planContents(workflow.history.loadPlan(plan.id).value()) == nookbit::PlanSignature::planContents(before.value()), "startup card inspection cannot change records");
    if (controller.recoveryCases().size() != 1) return;
    const auto row = controller.recoveryCases()[0].toMap();
    expect(row.value("moved").toInt() == 2, "recovery card must report completed prefix");
    controller.requestRecoveryChoice(QString::fromStdString(plan.id), "later");
    expect(ownedEventCount(database) == events, "native UI Later has no audit writes");
    if (scenario == 5) {
        expect(!row.value("canResume").toBool() && !row.value("canRollback").toBool(), "unsupported copy-before-start UI choices are disabled");
        controller.requestRecoveryChoice(QString::fromStdString(plan.id), "resume"); controller.confirmRecoveryChoice();
        expect(!controller.awaitingRecoveryConfirmation() && ownedEventCount(database) == events, "unsupported UI request cannot execute"); return;
    }
    const auto action = scenario == 1 || scenario == 4 ? "rollback" : "resume";
    controller.requestRecoveryChoice(QString::fromStdString(plan.id), action);
    expect(controller.awaitingRecoveryConfirmation() && ownedEventCount(database) == events, "native UI confirmation is not execution");
    controller.cancelRecoveryChoice(); expect(ownedEventCount(database) == events, "native confirmation cancellation is read-only");
    controller.requestRecoveryChoice(QString::fromStdString(plan.id), action);
    if (scenario == 2) current.setCollisionPolicy(nookbit::CollisionPolicy::Abort);
    controller.confirmRecoveryChoice(); controller.confirmRecoveryChoice();
    const auto after = workflow.history.loadPlan(plan.id);
    if (scenario == 2) {
        expect(after && before && after.value().status == before.value().status && ownedEventCount(database) == events &&
            nookbit::PlanSignature::planContents(after.value()) == nookbit::PlanSignature::planContents(before.value()), "changed current settings after confirmation cannot reuse old approval"); return;
    }
    expect(after && after.value().planHash == plan.planHash && after.value().status == (QString(action) == "resume" ? nookbit::PlanStatus::Completed : nookbit::PlanStatus::RolledBack),
        "native confirmed UI recovery finishes under original approval");
    for (const auto& step : plan.steps) {
        const auto at = QString(action) == "resume" ? step.destination : step.source;
        const auto hash = workflow.hasher.sha256File(at);
        expect(hash && hash.value() == *step.fingerprint.sha256 && !std::filesystem::exists(QString(action) == "resume" ? step.source : step.destination), "native UI recovery conserves original bytes");
    }
    if (scenario == 4 && partial) {
        expect(std::filesystem::file_size(partial->staging) == 1024 * 1024 && workflow.history.preservedTransfers(plan.id).value().size() == 1 &&
            controller.recoveryCases().size() == 1 && controller.recoveryCases()[0].toMap().value("preserved").toInt() == 1, "partial undo UI retains its separate staging reminder");
    } else expect(controller.recoveryCases().isEmpty(), "finished recovery must leave the interrupted list");
}

// 화면의 실제 미리보기·승인·이동·마지막/기록 되돌리기를 같은 서비스로 검사하는 함수.
void testNativeControllerCase(const QString& sourceRun, const QString& targetRun, int index) {
#ifdef _WIN32
    const auto sourceDir = QDir(sourceRun).filePath(QString("case-%1/incoming").arg(index));
    const auto targetDir = QDir(targetRun).filePath(QString("case-%1").arg(index));
    expect(QDir().mkpath(sourceDir) && QDir().mkpath(QDir(targetDir).filePath("incoming")), "owned native controller directories");
    const QStringList sources{QDir(sourceDir).filePath("first.jpg"), QDir(targetDir).filePath("incoming/second.jpg"), QDir(sourceDir).filePath("third.jpg")};
    for (int i = 0; i < sources.size(); ++i) {
        expect(!QFile::exists(sources[i]), "native controller test never overwrites existing sources");
        QFile file(sources[i]);
        expect(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(QByteArray("owned native UI bytes ") + QByteArray::number(i)) > 0, "native controller source creation");
    }
    const auto home = QDir(targetDir).filePath("organized");
    const auto appData = std::filesystem::path(QDir(targetDir).filePath("app-data").toStdWString());
    PreviewWorkflow workflow(projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), appData, true);
    nookbit::PendingFilesModel pending;
    for (const auto& source : sources) pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController controller(pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase, workflow.approveUseCase,
        workflow.history, workflow.operations, projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), home);
    controller.createPreview(home);
    expect(controller.hasPreview() && controller.steps().size() == 3, "native UI should preview every selected file");
    if (controller.steps().size() != 3) { std::cerr << controller.message().toStdString() << '\n'; return; }
    controller.moveApprovedFile();
    for (const auto& source : sources) expect(QFile::exists(source), "native UI cannot move before approval");
    controller.freezePreview(); controller.approvePlan();
    expect(controller.hasApprovedPlan(), "native UI should approve the stored v2 contracts");
    const auto entries = workflow.history.incompletePlans();
    if (!entries || entries.value().empty()) { expect(false, "native UI frozen history missing"); return; }
    const auto loaded = workflow.history.loadPlan(entries.value()[0].id);
    if (!loaded) { expect(false, "native UI signed plan missing"); return; }
    const auto plan = loaded.value();
    expect(plan.formatVersion == 2 && plan.status == nookbit::PlanStatus::Approved, "native UI must store approved v2, never downgrade to v1");
    for (const auto& step : plan.steps) {
        const auto a = workflow.copy.inspectVolume(step.source), b = workflow.copy.inspectVolume(step.destination);
        expect(a && b && step.movement && step.movement->strategy ==
            (a.value().name == b.value().name ? nookbit::MoveStrategy::Rename : nookbit::MoveStrategy::VerifiedCopy), "UI contracts must reflect real volume identities");
    }
    const auto destination = QString::fromStdWString(plan.steps[0].destination.wstring());
    if (index == 2) {
        expect(QDir().mkpath(QFileInfo(destination).absolutePath()), "late UI collision parent");
        QFile occupant(destination); expect(occupant.open(QIODevice::WriteOnly | QIODevice::NewOnly) && occupant.write("owned late occupant") > 0, "late UI collision fixture");
    }
    if (index == 3) { QFile source(sources[0]); expect(source.open(QIODevice::Append) && source.write(" changed") > 0, "changed approved UI source"); }
    controller.moveApprovedFile();
    const auto moved = workflow.history.loadPlan(plan.id);
    if (index == 2 || index == 3) {
        expect(moved && moved.value().status == nookbit::PlanStatus::Approved && pending.count() == 3 && !controller.canUndo(), "UI rejection must not start movement or remove pending files");
        for (const auto& source : sources) expect(QFile::exists(source), "rejected UI move preserves every source");
        for (size_t i = 0; i < plan.steps.size(); ++i) if (index != 2 || i != 0) expect(!std::filesystem::exists(plan.steps[i].destination), "rejected UI move must not create another final file");
        if (index == 2) { QFile occupant(destination); expect(occupant.open(QIODevice::ReadOnly) && occupant.readAll() == "owned late occupant", "UI collision occupant unchanged"); }
        return;
    }
    expect(moved && moved.value().status == nookbit::PlanStatus::Completed && pending.count() == 0 && controller.canUndo(), "native UI move must complete and update pending/undo state");
    if (!moved || moved.value().status != nookbit::PlanStatus::Completed) { std::cerr << controller.message().toStdString() << '\n'; return; }
    for (const auto& step : plan.steps) {
        const auto attempts = workflow.journal.forStep(step.id);
        expect(attempts && attempts.value().size() == (step.movement->strategy == nookbit::MoveStrategy::VerifiedCopy ? 1 : 0), "UI movement must use exactly the expected transfer records");
        const auto hash = workflow.hasher.sha256File(step.destination);
        expect(!std::filesystem::exists(step.source) && hash && hash.value() == *step.fingerprint.sha256, "UI moved contents must match approved SHA-256");
    }
    if (index == 4) {
        QFile occupant(sources[0]); expect(occupant.open(QIODevice::WriteOnly | QIODevice::NewOnly) && occupant.write("owned undo occupant") > 0, "UI undo collision fixture"); occupant.close();
        controller.undoLastMove();
        expect(workflow.history.loadPlan(plan.id).value().status == nookbit::PlanStatus::Completed && controller.canUndo(), "rejected UI undo must retain completed history and retry availability");
        for (const auto& step : plan.steps) expect(std::filesystem::exists(step.destination), "undo preflight collision must leave all moved files in place");
        QFile preserved(sources[0]); expect(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == "owned undo occupant", "UI undo must not overwrite occupant");
        return;
    }
    if (index == 1) {
        PreviewWorkflow restarted(projectPath("resources/rules/default.v3.json"), projectPath("schemas/rules-tree-v3.schema.json"), appData, true);
        nookbit::HistoryController history(restarted.history, restarted.operations);
        QObject::connect(&history, &nookbit::HistoryController::rollbackAttempted, &controller, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
        history.refreshHistory(); history.selectHistoryPlan(QString::fromStdString(plan.id));
        expect(history.canUndoSelectedHistory(), "reopened history must offer native v2 undo"); history.undoSelectedHistory();
    } else controller.undoLastMove();
    const auto restored = workflow.history.loadPlan(plan.id);
    expect(restored && restored.value().status == nookbit::PlanStatus::RolledBack && !controller.canUndo(), "native UI undo must finish and clear the last action");
    for (const auto& step : plan.steps) {
        const auto hash = workflow.hasher.sha256File(step.source);
        expect(hash && hash.value() == *step.fingerprint.sha256 && !std::filesystem::exists(step.destination), "native UI undo restores each approved original");
        const auto attempts = workflow.journal.forStep(step.id);
        expect(attempts && attempts.value().size() == (step.movement->strategy == nookbit::MoveStrategy::VerifiedCopy ? 2 : 0), "native UI undo must use reverse copy only for cross-volume movement");
    }
#else
    (void)sourceRun; (void)targetRun; (void)index;
#endif
}

// 명시한 두 부모 안에 새 고유 자료만 만들어 실제 화면 연결을 시험하는 함수.
int runNativeControllerTests(const QStringList& args) {
    if (args.size() != 6 || args[2] != "--source-parent" || args[4] != "--destination-parent") return 2;
    for (const auto& parent : {args[3], args[5]}) if (!QFileInfo(parent).isAbsolute() || !QFileInfo(parent).isDir()) return 2;
    nookbit::WindowsStagedCopy volumes;
    const auto a = volumes.inspectVolume(std::filesystem::path(args[3].toStdWString())), b = volumes.inspectVolume(std::filesystem::path(args[5].toStdWString()));
    if (!a || !b || !a.value().localNtfs || !b.value().localNtfs || a.value().name == b.value().name) return 2;
    nookbit::OperationMutex lock;
    if (!lock.acquire()) return 2;
    QTemporaryDir source(QDir(args[3]).filePath("NookBit-ui-source-XXXXXX")), target(QDir(args[5]).filePath("NookBit-ui-target-XXXXXX"));
    source.setAutoRemove(false); target.setAutoRemove(false);
    if (!source.isValid() || !target.isValid()) return 2;
    std::cout << "Source fixtures: " << source.path().toStdString() << "\nTarget fixtures/report: " << target.path().toStdString() << '\n';
    const QStringList names{"preview-mixed-move-last-undo", "reopened-history-mixed-undo", "late-destination-collision", "changed-approved-source", "undo-preflight-collision"};
    QJsonArray cases;
    for (int i = 0; i < names.size(); ++i) {
        const auto before = failures; testNativeControllerCase(source.path(), target.path(), i);
        cases.append(QJsonObject{{"case", names[i]}, {"passed", failures == before}});
        std::cout << (failures == before ? "PASS " : "FAIL ") << names[i].toStdString() << '\n';
    }
    const QStringList recoveryNames{"confirmed-mixed-resume", "confirmed-mixed-undo", "changed-settings-after-confirmation", "confirmed-partial-resume", "confirmed-partial-preserving-undo", "unsupported-start-disabled"};
    for (int i = 0; i < recoveryNames.size(); ++i) {
        const auto before = failures; testNativeRecoveryChoice(source.path(), target.path(), i);
        cases.append(QJsonObject{{"case", recoveryNames[i]}, {"passed", failures == before}});
        std::cout << (failures == before ? "PASS " : "FAIL ") << recoveryNames[i].toStdString() << '\n';
    }
    QSaveFile report(QDir(target.path()).filePath("ui-backend-report.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"sourceRoot", source.path()}, {"targetRoot", target.path()}, {"passed", failures == 0}, {"cases", cases}}).toJson();
    if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size() || !report.commit()) return 2;
    std::cout << "Owned fixtures retained; no recursive cleanup.\n";
    return failures ? 1 : 0;
}
} // namespace

// 미리보기 화면 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() > 1 && application.arguments()[1] == "--background-cross-run") return runBackgroundCrossTests(application.arguments());
    if (application.arguments().size() > 1 && application.arguments()[1] == "--native-cross-run") return runNativeControllerTests(application.arguments());
    if (application.arguments().size() > 1) return 2;
    testBackgroundRunner();
    testBackgroundApprovalSafety();
    QTemporaryDir backgroundSameVolume;
    if (backgroundSameVolume.isValid()) testBackgroundWorkflow(backgroundSameVolume.path(), backgroundSameVolume.path());
    QTemporaryDir nativeSameVolume;
    if (nativeSameVolume.isValid()) testNativeControllerCase(nativeSameVolume.path(), nativeSameVolume.path(), 0);
    else expect(false, "native v2 same-volume UI fixture");
    testCommonCollisionPolicyWorkflow();
    testWatchedFilesUseNormalSortWorkflow();
    testSortConfirmationSafety();
    testEveryPreviewOutcomeIsVisible();
    testSavedEditorRulesReachMainPreview();
    testPreviewFromPendingFiles();
    testTreeRulesReachPreview();
    testChangedFileCannotBeFrozen();
    testChangedRulesCannotBeFrozen();
    testChangedSettingsCannotBeFrozen();
    testChangedFileAfterSaveFailsVerification();
    testTamperedSavedPlanFailsVerification();
    testSingleFileApproveMoveUndo();
    testMultipleFilesCanBeMoved();
    testHistoryUndoUpdatesLastMove();
    testHistoryUndoAfterRestart();
    testRecoveryCasesInController();
    testV2StartupRecoveryNotice();
    testRecoveryConfirmationFlow();
    if (failures == 0) {
        std::cout << "All preview-controller tests passed.\n";
        return 0;
    }
    std::cerr << failures << " preview-controller assertion(s) failed.\n";
    return 1;
}
