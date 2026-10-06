#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/interrupted_operation_recovery_service.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트 내부의 예제나 Schema 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

// 테스트에서 목적지 설정 하나를 제공하는 저장소.
class TestSettingsRepository final : public nookbit::ISettingsRepository {
public:
    nookbit::UserSettings settings;

    // 현재 테스트 설정을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::UserSettings> load() const override {
        return nookbit::Result<nookbit::UserSettings>::success(settings);
    }

    // 테스트에서 설정 저장을 거절하는 함수.
    [[nodiscard]] nookbit::Result<void> save(const nookbit::UserSettings&) override {
        return nookbit::Result<void>::failure({"test.read_only", "Not needed here."});
    }
};

struct ApprovedBundle {
    nookbit::OperationPlan plan;
    nookbit::ApprovalRecord approval;
};

// 독립된 임시 파일과 SQLite를 준비하는 테스트 환경.
struct Scenario {
    QTemporaryDir directory;
    QString incoming = directory.filePath(QStringLiteral("incoming"));
    QString source = QDir(incoming).filePath(QStringLiteral("photo.jpg"));
    QString home = directory.filePath(QStringLiteral("organized"));
    QString appData = directory.filePath(QStringLiteral("app-data"));
    QString databasePath = QDir(appData).filePath(QStringLiteral("history.sqlite"));
    TestSettingsRepository settings;
    nookbit::JsonRuleSetRepository rules{
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json")};
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine ruleEngine;
    nookbit::Planner planner{ruleEngine, std::filesystem::path(appData.toStdWString())};
    nookbit::QtPlanHasher hasher;
    nookbit::WindowsStagedCopy volumes;
    nookbit::SqliteHistoryRepository history{
        databasePath,
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql")};
    bool ready{false};

    // 임시 원본 파일과 기본 목적지 설정을 만드는 생성자.
    Scenario() {
        ready = directory.isValid() && QDir().mkpath(incoming);
        if (!ready) return;
        QFile file(source);
        ready = file.open(QIODevice::WriteOnly) && file.write("original bytes") == 14;
        settings.settings.homeRoot = std::filesystem::path(home.toStdWString());
    }

    // 임시 원본 파일의 표준 경로를 반환하는 함수.
    [[nodiscard]] std::filesystem::path sourcePath() const {
        return std::filesystem::path(source.toStdWString());
    }

    // 선택한 파일들의 계획을 미리보기·고정·검증 후 승인하는 함수.
    [[nodiscard]] nookbit::Result<ApprovedBundle> approveFiles(
        const std::vector<std::filesystem::path>& selected, bool native = false) {
        auto loadedRules = rules.load();
        if (!loadedRules) return nookbit::Result<ApprovedBundle>::failure(loadedRules.error());
        nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher, native ? &volumes : nullptr);
        auto draft = preview.execute({loadedRules.value().version, selected});
        if (!draft) return nookbit::Result<ApprovedBundle>::failure(draft.error());
        nookbit::FreezePreviewUseCase freeze(rules, scanner, planner, hasher, history, native ? &volumes : nullptr);
        auto frozen = freeze.execute(draft.value(), selected, settings.settings);
        if (!frozen) return nookbit::Result<ApprovedBundle>::failure(frozen.error());
        nookbit::VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history, native ? &volumes : nullptr);
        nookbit::ApprovePlanUseCase approver(verifier, history);
        auto approval = approver.execute(frozen.value().id, selected, settings.settings);
        if (!approval) return nookbit::Result<ApprovedBundle>::failure(approval.error());
        auto plan = history.loadPlan(frozen.value().id);
        if (!plan) return nookbit::Result<ApprovedBundle>::failure(plan.error());
        return nookbit::Result<ApprovedBundle>::success({plan.value(), approval.value()});
    }

    // 기본 사진 파일 하나의 승인 계획을 만드는 함수.
    [[nodiscard]] nookbit::Result<ApprovedBundle> approve() {
        return approveFiles({sourcePath()});
    }
};

// 이동 의도만 기록된 상태에서는 원본을 그대로 두고 실패로 정리하는지 검사하는 함수.
void testIntentBeforeMove() {
    Scenario scenario;
    expect(scenario.ready, "pre-move scenario should be ready");
    if (!scenario.ready) return;
    auto approved = scenario.approve();
    expect(approved.hasValue(), "pre-move plan should be approved");
    if (!approved) return;
    const auto& plan = approved.value().plan;
    const auto& step = plan.steps.front();
    expect(step.fingerprint.sha256.has_value(),
           "recovery plan should include a content hash");
    expect(scenario.history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(),
           "executing intent should be recorded");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(),
           "step intent should be recorded");

    nookbit::RecoveryService recovery(scenario.history, scenario.scanner, scenario.hasher);
    const auto result = recovery.reconcile();
    expect(result.hasValue() && result.value().reconciled == 1 &&
               result.value().needsAttention.empty(),
           "unchanged source should reconcile without file movement");
    const auto saved = scenario.history.loadPlan(plan.id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::Failed &&
               saved.value().steps.front().status == nookbit::StepStatus::Failed,
           "pre-move intent should become a failed plan");
    expect(QFile::exists(scenario.source) &&
               !QFile::exists(QString::fromStdWString(step.destination.wstring())),
           "recovery must leave an unmoved source untouched");
}

// 파일은 이동됐지만 완료 기록 전 종료된 상태를 복원하는지 검사하는 함수.
void testMoveBeforeCompletionRecord() {
    Scenario scenario;
    expect(scenario.ready, "moved scenario should be ready");
    if (!scenario.ready) return;
    auto approved = scenario.approve();
    expect(approved.hasValue(), "moved plan should be approved");
    if (!approved) return;
    const auto& plan = approved.value().plan;
    const auto& step = plan.steps.front();
    expect(scenario.history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(),
           "executing state should be recorded");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(),
           "move intent should be recorded");
    expect(nookbit::WindowsFileMover::moveNoReplace(step.source, step.destination).hasValue(),
           "test should simulate a completed move before logging");

    nookbit::RecoveryService recovery(scenario.history, scenario.scanner, scenario.hasher);
    const auto result = recovery.reconcile();
    expect(result.hasValue() && result.value().reconciled == 1,
           "moved file should reconcile as completed");
    const auto saved = scenario.history.loadPlan(plan.id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::Completed &&
               saved.value().steps.front().status == nookbit::StepStatus::Applied,
           "DB should record the already-completed move");
    expect(!QFile::exists(scenario.source) &&
               QFile::exists(QString::fromStdWString(step.destination.wstring())),
           "reconciliation must not move the file a second time");

    nookbit::SafeExecutor executor(
        scenario.settings, scenario.scanner,
        scenario.hasher, scenario.history,
        std::filesystem::path(scenario.appData.toStdWString()));
    expect(executor.rollback(plan.id).hasValue(),
           "reconciled completed move should still be undoable");
    expect(QFile::exists(scenario.source), "undo should restore the original file");
}

// 되돌리기는 끝났지만 DB 기록 전 종료된 상태를 복원하는지 검사하는 함수.
void testUndoBeforeCompletionRecord() {
    Scenario scenario;
    expect(scenario.ready, "undo scenario should be ready");
    if (!scenario.ready) return;
    auto approved = scenario.approve();
    expect(approved.hasValue(), "undo plan should be approved");
    if (!approved) return;
    const auto& plan = approved.value().plan;
    const auto& step = plan.steps.front();
    nookbit::VerifySavedPlanUseCase verifier(
        scenario.rules, scenario.scanner, scenario.planner,
        scenario.hasher, scenario.history);
    nookbit::SafeExecutor executor(
        scenario.settings, verifier, scenario.scanner,
        scenario.hasher, scenario.history,
        std::filesystem::path(scenario.appData.toStdWString()));
    expect(executor.execute(plan, approved.value().approval, {scenario.sourcePath()}).hasValue(),
           "test should first complete a real move");
    expect(scenario.history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::RollingBack}).hasValue(),
           "rollback state should be recorded");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::RollbackIntentRecorded}).hasValue(),
           "rollback intent should be recorded");
    expect(nookbit::WindowsFileMover::moveNoReplace(step.destination, step.source).hasValue(),
           "test should simulate undo before final log entry");

    nookbit::RecoveryService recovery(scenario.history, scenario.scanner, scenario.hasher);
    const auto result = recovery.reconcile();
    expect(result.hasValue() && result.value().reconciled == 1,
           "completed undo should reconcile");
    const auto saved = scenario.history.loadPlan(plan.id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::RolledBack &&
               saved.value().steps.front().status == nookbit::StepStatus::RolledBack,
           "DB should record the already-completed undo");
    expect(QFile::exists(scenario.source) &&
               !QFile::exists(QString::fromStdWString(step.destination.wstring())),
           "reconciliation must not move the file a second time");
}

// 되돌리기 의도만 기록되고 파일은 그대로면 완료 상태로 되감는지 검사하는 함수.
void testUndoIntentBeforeMove() {
    Scenario scenario;
    expect(scenario.ready, "undo-intent scenario should be ready");
    if (!scenario.ready) return;
    auto approved = scenario.approve();
    expect(approved.hasValue(), "undo-intent plan should be approved");
    if (!approved) return;
    const auto& plan = approved.value().plan;
    const auto& step = plan.steps.front();
    nookbit::VerifySavedPlanUseCase verifier(
        scenario.rules, scenario.scanner, scenario.planner,
        scenario.hasher, scenario.history);
    nookbit::SafeExecutor executor(
        scenario.settings, verifier, scenario.scanner,
        scenario.hasher, scenario.history,
        std::filesystem::path(scenario.appData.toStdWString()));
    expect(executor.execute(plan, approved.value().approval, {scenario.sourcePath()}).hasValue(),
           "test should first complete a real move");
    expect(scenario.history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::RollingBack}).hasValue(),
           "rollback state should be recorded");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::RollbackIntentRecorded}).hasValue(),
           "rollback intent should be recorded");

    nookbit::RecoveryService recovery(scenario.history, scenario.scanner, scenario.hasher);
    const auto result = recovery.reconcile();
    expect(result.hasValue() && result.value().reconciled == 1,
           "unmoved rollback intent should reconcile");
    const auto saved = scenario.history.loadPlan(plan.id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::Completed &&
               saved.value().steps.front().status == nookbit::StepStatus::Applied,
           "unmoved rollback should return to the completed state");
    expect(!QFile::exists(scenario.source) &&
               QFile::exists(QString::fromStdWString(step.destination.wstring())),
           "reconciliation must leave the destination file untouched");
    expect(executor.rollback(plan.id).hasValue(),
           "a reconciled unmoved rollback should be retryable");
    expect(QFile::exists(scenario.source), "retried undo should restore the source");
}

// 출발지와 목적지 상태가 모호하면 파일을 건드리지 않고 확인 필요로 남기는 함수.
void testAmbiguousFilesNeedAttention() {
    Scenario scenario;
    expect(scenario.ready, "ambiguous scenario should be ready");
    if (!scenario.ready) return;
    auto approved = scenario.approve();
    expect(approved.hasValue(), "ambiguous plan should be approved");
    if (!approved) return;
    const auto& plan = approved.value().plan;
    const auto& step = plan.steps.front();
    expect(scenario.history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(),
           "ambiguous executing state should be recorded");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(),
           "ambiguous step intent should be recorded");
    const auto destination = QString::fromStdWString(step.destination.wstring());
    expect(QDir().mkpath(QFileInfo(destination).absolutePath()),
           "ambiguous destination parent should be created");
    expect(QFile::copy(scenario.source, destination),
           "a distinct destination copy should be created");

    nookbit::RecoveryService recovery(scenario.history, scenario.scanner, scenario.hasher);
    const auto result = recovery.reconcile();
    expect(result.hasValue() && result.value().needsAttention.size() == 1,
           "ambiguous files should require attention");
    const auto saved = scenario.history.loadPlan(plan.id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::NeedsRecovery &&
               saved.value().steps.front().status == nookbit::StepStatus::NeedsRecovery,
           "ambiguous journal should be marked for manual recovery");
    expect(QFile::exists(scenario.source) && QFile::exists(destination),
           "ambiguous recovery must not delete or move either file");
    const auto repeated = recovery.reconcile();
    expect(repeated.hasValue() && repeated.value().needsAttention.size() == 1,
           "repeated recovery should keep unresolved plan visible");
    const auto cases = recovery.inspectCases();
    expect(cases.hasValue() && cases.value().size() == 1 && !cases.value().front().canResolve,
           "ambiguous manual recovery should not offer a completion action");
    expect(!recovery.resolveCase(plan.id).hasValue(),
           "manual confirmation must refuse two occupied file locations");
    expect(QFile::exists(scenario.source) && QFile::exists(destination),
           "refused manual recovery must leave both files untouched");
    expect(QFile::remove(destination), "test copy should be removed before reinspection");
    const auto inspected = recovery.inspectCases();
    expect(inspected.hasValue() && inspected.value().size() == 1 &&
               inspected.value().front().canResolve,
           "manual recovery should become available when one matching file remains");
    expect(recovery.resolveCase(plan.id).hasValue(),
           "manual recovery should confirm the verified source-only state");
    const auto resolved = scenario.history.loadPlan(plan.id);
    expect(resolved.hasValue() && resolved.value().status == nookbit::PlanStatus::Failed &&
               resolved.value().steps.front().status == nookbit::StepStatus::Failed,
           "source-only interrupted move should be recorded as failed");
    expect(QFile::exists(scenario.source) && !QFile::exists(destination),
           "manual recovery should not move the remaining file");
}

// 배치가 중간에 끊기면 모든 파일을 보여주고 동일한 위치로 확인될 때만 정리하는 함수.
void testInterruptedBatchNeedsManualReview() {
    Scenario scenario;
    expect(scenario.ready, "batch-recovery scenario should be ready");
    if (!scenario.ready) return;
    const auto second = QDir(scenario.incoming).filePath(QStringLiteral("second.jpg"));
    QFile secondFile(second);
    expect(secondFile.open(QIODevice::WriteOnly) && secondFile.write("second bytes") == 12,
           "batch-recovery second source should be written");
    secondFile.close();
    const auto secondPath = std::filesystem::path(second.toStdWString());
    auto approved = scenario.approveFiles({scenario.sourcePath(), secondPath});
    expect(approved.hasValue() && approved.value().plan.steps.size() == 2,
           "batch-recovery plan should contain two moves");
    if (!approved || approved.value().plan.steps.size() != 2) return;
    const auto& plan = approved.value().plan;
    const auto& firstStep = plan.steps.front();
    expect(scenario.history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(),
           "batch-recovery execution should begin");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, firstStep.id, nookbit::StepStatus::IntentRecorded}).hasValue(),
           "batch-recovery first intent should be recorded");
    expect(nookbit::WindowsFileMover::moveNoReplace(firstStep.source, firstStep.destination).hasValue(),
           "batch-recovery first file should move before interruption");
    expect(scenario.history.appendEvent(
               nookbit::StepStatusChanged{plan.id, firstStep.id, nookbit::StepStatus::Applied}).hasValue(),
           "batch-recovery first application should be recorded");
    nookbit::SqliteHistoryRepository restartedHistory(
        scenario.databasePath, projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    nookbit::FilesystemScanner restartedScanner;
    nookbit::QtPlanHasher restartedHasher;
    nookbit::RecoveryService recovery(restartedHistory, restartedScanner, restartedHasher);
    auto reconciled = recovery.reconcile();
    expect(reconciled.hasValue() && reconciled.value().needsAttention.size() == 1,
           "interrupted batch should require user review");
    auto cases = recovery.inspectCases();
    expect(cases.hasValue() && cases.value().size() == 2 &&
               !cases.value().at(0).canResolve && !cases.value().at(1).canResolve,
           "mixed file positions should be visible but not confirmable");
    expect(!recovery.resolveCase(plan.id).hasValue(),
           "mixed batch positions must not be confirmed");
    expect(nookbit::WindowsFileMover::moveNoReplace(firstStep.destination, firstStep.source).hasValue(),
           "test should simulate user's manual return of the first file");
    cases = recovery.inspectCases();
    expect(cases.hasValue() && cases.value().size() == 2 &&
               cases.value().at(0).canResolve && cases.value().at(1).canResolve,
           "source-only batch should be confirmable");
    expect(recovery.resolveCase(plan.id).hasValue(),
           "verified source-only batch should clear the recovery record");
    auto saved = scenario.history.loadPlan(plan.id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::Failed &&
               saved.value().steps.at(0).status == nookbit::StepStatus::Failed &&
               saved.value().steps.at(1).status == nookbit::StepStatus::Failed,
           "manually returned batch should be recorded as failed without moving files");
    expect(QFile::exists(scenario.source) && QFile::exists(second),
           "manual confirmation should preserve both source files");
}

// 테스트가 소유한 새 원본만 만들어 승인 목록에 추가하는 함수.
std::filesystem::path addOwnedSource(Scenario& scenario, const QString& name) {
    const auto source = QDir(scenario.incoming).filePath(name);
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write("untouched original") == 18, "owned recovery regression source");
    return std::filesystem::path(source.toStdWString());
}

// 앱 시작의 구형 복구가 v2의 복원 완료 증거를 바꾸지 않는지 검사하는 함수.
void testLegacyStartupPreservesV2Undo() {
    Scenario scenario;
    if (!scenario.ready) { expect(false, "v2 startup fixture"); return; }
    const auto second = addOwnedSource(scenario, "second.jpg");
    const std::vector<std::filesystem::path> selected{scenario.sourcePath(), second};
    const auto approved = scenario.approveFiles(selected, true);
    if (!approved) { expect(false, "v2 startup approval"); return; }
    const auto& plan = approved.value().plan;
    nookbit::VerifySavedPlanUseCase verifier(scenario.rules, scenario.scanner, scenario.planner, scenario.hasher, scenario.history, &scenario.volumes);
    nookbit::SafeExecutor executor(scenario.settings, verifier, scenario.scanner, scenario.hasher, scenario.history,
        std::filesystem::path(scenario.appData.toStdWString()), &scenario.volumes);
    expect(executor.execute(plan, approved.value().approval, selected).hasValue(), "v2 startup rename batch completes");
    expect(scenario.history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::RollingBack}).hasValue(), "v2 interrupted undo starts");
    expect(scenario.history.appendEvent(nookbit::StepStatusChanged{plan.id, plan.steps[0].id, nookbit::StepStatus::RollbackIntentRecorded}).hasValue() &&
        nookbit::WindowsFileMover::moveNoReplace(plan.steps[0].destination, plan.steps[0].source).hasValue() &&
        scenario.history.appendEvent(nookbit::StepStatusChanged{plan.id, plan.steps[0].id, nookbit::StepStatus::RolledBack}).hasValue(), "first v2 rename restored before interruption");
    const auto before = scenario.history.loadPlan(plan.id);
    nookbit::SqliteTransferJournal journal(scenario.databasePath, projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    nookbit::RecoveryService legacy(scenario.history, scenario.scanner, scenario.hasher, &journal);
    for (int pass = 0; pass < 2; ++pass) {
        const auto result = legacy.reconcile();
        const auto after = scenario.history.loadPlan(plan.id);
        expect(result && result.value().reconciled == 0 && result.value().needsAttention.size() == 1 && before && after &&
            after.value().status == before.value().status && nookbit::PlanSignature::planContents(after.value()) == nookbit::PlanSignature::planContents(before.value()),
            "legacy startup must not rewrite v2 RolledBack/Applied step evidence");
    }
    const auto visible = legacy.inspectCases();
    expect(visible && visible.value().size() == 2 && !visible.value()[0].canResolve && !visible.value()[1].canResolve,
        "preserved v2 interrupted states must remain visible without offering legacy resolution");
    nookbit::WindowsTransferFileInspector files;
    nookbit::InspectInterruptedPlanUseCase inspector(scenario.history, journal, files, scenario.hasher, &scenario.volumes);
    const auto report = inspector.execute(plan.id);
    expect(report && report.value().restored == 1 && report.value().moved == 1 && report.value().needsAttention == 0,
        "startup-preserved v2 undo must remain diagnosable as restored plus moved");
    nookbit::RecoverInterruptedPlanUseCase recover(inspector, scenario.history, scenario.settings, scenario.rules, scenario.hasher, executor);
    nookbit::InterruptedOperationRecoveryService service(recover);
    expect(service.execute({plan.id, plan.planHash, nookbit::RecoveryAction::Rollback}).hasValue(), "explicit v2 undo remains possible after startup inspection");
    const auto final = scenario.history.loadPlan(plan.id);
    expect(final && final.value().status == nookbit::PlanStatus::RolledBack && final.value().planHash == plan.planHash, "v2 undo finishes with original approval");
}

// 복구 되돌리기 충돌이 아직 시작하지 않은 단계의 증거를 보존하는지 검사하는 함수.
void testUndoFailurePreservesPending(bool native) {
    Scenario scenario;
    if (!scenario.ready) { expect(false, "pending recovery fixture"); return; }
    const auto second = addOwnedSource(scenario, "pending-second.jpg"), third = addOwnedSource(scenario, "pending-third.jpg");
    const std::vector<std::filesystem::path> selected{scenario.sourcePath(), second, third};
    const auto approved = scenario.approveFiles(selected, native);
    if (!approved) { expect(false, "pending recovery approval"); return; }
    const auto& plan = approved.value().plan;
    expect(scenario.history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue() &&
        scenario.history.appendEvent(nookbit::StepStatusChanged{plan.id, plan.steps[0].id, nookbit::StepStatus::IntentRecorded}).hasValue() &&
        nookbit::WindowsFileMover::moveNoReplace(plan.steps[0].source, plan.steps[0].destination).hasValue() &&
        scenario.history.appendEvent(nookbit::StepStatusChanged{plan.id, plan.steps[0].id, nookbit::StepStatus::Applied}).hasValue() &&
        scenario.history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::RollingBack}).hasValue(), "owned interrupted undo with two pending originals");
    QFile occupant(scenario.source);
    expect(occupant.open(QIODevice::WriteOnly | QIODevice::NewOnly) && occupant.write("owned collision") == 15, "late undo collision occupant"); occupant.close();
    nookbit::VerifySavedPlanUseCase verifier(scenario.rules, scenario.scanner, scenario.planner, scenario.hasher, scenario.history, native ? &scenario.volumes : nullptr);
    nookbit::SafeExecutor executor(scenario.settings, verifier, scenario.scanner, scenario.hasher, scenario.history,
        std::filesystem::path(scenario.appData.toStdWString()), native ? &scenario.volumes : nullptr);
    const auto interrupted = scenario.history.loadPlan(plan.id);
    if (!interrupted) { expect(false, "pending recovery plan load"); return; }
    expect(!executor.runRecoveredPlan(interrupted.value(), nookbit::RecoveryAction::Rollback), "undo collision must stop without overwriting");
    const auto blocked = scenario.history.loadPlan(plan.id);
    expect(blocked && blocked.value().status == nookbit::PlanStatus::NeedsRecovery &&
        blocked.value().steps[0].status == nookbit::StepStatus::NeedsRecovery && blocked.value().steps[1].status == nookbit::StepStatus::Planned &&
        blocked.value().steps[2].status == nookbit::StepStatus::Planned, "undo failure must retain never-started Planned steps");
    QFile preserved(scenario.source); expect(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == "owned collision", "collision occupant remains unchanged"); preserved.close();
    // 다른 파일이 아니라 이번 시험이 만든 점유 파일만 제거해 재시도 경계를 준비한다.
    expect(QFile::remove(scenario.source), "remove only owned regression collision occupant");
    nookbit::SqliteTransferJournal journal(scenario.databasePath, projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    nookbit::WindowsTransferFileInspector files;
    nookbit::InspectInterruptedPlanUseCase inspector(scenario.history, journal, files, scenario.hasher, native ? &scenario.volumes : nullptr);
    const auto report = inspector.execute(plan.id);
    expect(report && report.value().moved == 1 && report.value().pending == 2 && report.value().needsAttention == 0,
        "collision cleared: moved/pending evidence still permits explicit undo");
    nookbit::RecoverInterruptedPlanUseCase recover(inspector, scenario.history, scenario.settings, scenario.rules, scenario.hasher, executor);
    nookbit::InterruptedOperationRecoveryService service(recover);
    expect(service.execute({plan.id, plan.planHash, nookbit::RecoveryAction::Rollback}).hasValue(), "undo retry uses preserved pending evidence");
    const auto final = scenario.history.loadPlan(plan.id);
    expect(final && final.value().status == nookbit::PlanStatus::RolledBack && final.value().steps[1].status == nookbit::StepStatus::Planned &&
        final.value().steps[2].status == nookbit::StepStatus::Planned, "undo retry does not move never-started files");
    for (const auto& step : plan.steps) {
        const auto hash = scenario.hasher.sha256File(step.source);
        expect(hash && hash.value() == *step.fingerprint.sha256 && !std::filesystem::exists(step.destination), "regression undo conserves original contents");
    }
}
} // namespace

// 중단 시점별 복구 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testIntentBeforeMove();
    testMoveBeforeCompletionRecord();
    testUndoBeforeCompletionRecord();
    testUndoIntentBeforeMove();
    testAmbiguousFilesNeedAttention();
    testInterruptedBatchNeedsManualReview();
    testLegacyStartupPreservesV2Undo();
    testUndoFailurePreservesPending(false);
    testUndoFailurePreservesPending(true);
    if (failures == 0) {
        std::cout << "All recovery-service tests passed.\n";
        return 0;
    }
    std::cerr << failures << " recovery-service assertion(s) failed.\n";
    return 1;
}
