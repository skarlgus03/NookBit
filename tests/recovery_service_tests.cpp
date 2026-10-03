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
        const std::vector<std::filesystem::path>& selected) {
        auto loadedRules = rules.load();
        if (!loadedRules) return nookbit::Result<ApprovedBundle>::failure(loadedRules.error());
        nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher);
        auto draft = preview.execute({loadedRules.value().version, selected});
        if (!draft) return nookbit::Result<ApprovedBundle>::failure(draft.error());
        nookbit::FreezePreviewUseCase freeze(rules, scanner, planner, hasher, history);
        auto frozen = freeze.execute(draft.value(), selected, settings.settings);
        if (!frozen) return nookbit::Result<ApprovedBundle>::failure(frozen.error());
        nookbit::VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history);
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
    if (failures == 0) {
        std::cout << "All recovery-service tests passed.\n";
        return 0;
    }
    std::cerr << failures << " recovery-service assertion(s) failed.\n";
    return 1;
}
