#include <nookbit/application/recover_interrupted_plan_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"

namespace nookbit {
// 외부 일꾼을 생성하지 않고 사용자 선택에 필요한 포트만 받는 함수.
RecoverInterruptedPlanUseCase::RecoverInterruptedPlanUseCase(InspectInterruptedPlanUseCase& inspector,
    IHistoryRepository& history, const ISettingsRepository& settings, const IRuleSetRepository& rules,
    const IPlanHasher& hasher, IRecoveredPlanRunner& runner)
    : inspector_(inspector), history_(history), settings_(settings), rules_(rules), hasher_(hasher), runner_(runner) {}

// 선택한 순간의 상태를 검사하고 기록이 성공한 뒤에만 실행을 요청하는 함수.
Result<void> RecoverInterruptedPlanUseCase::execute(const RecoveryRequest& request) {
    if (request.action == RecoveryAction::Later) return Result<void>::success();
    if (request.action != RecoveryAction::Resume && request.action != RecoveryAction::Rollback)
        return Result<void>::failure({"recovery.action_invalid", "Unknown recovery choice."});
    const auto loaded = history_.loadPlan(request.planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    const auto& plan = loaded.value();
    if (!PlanSignature::isSha256Hex(request.planHash) || request.planHash != plan.planHash)
        return Result<void>::failure({"recovery.choice_stale", "The chosen approval snapshot no longer matches."});
    const auto settings = settings_.load();
    if (!settings) return Result<void>::failure(settings.error());
    const auto rules = rules_.load();
    if (!rules) return Result<void>::failure(rules.error());
    if (plan.settingsRevision != hasher_.sha256(PlanSignature::settings(settings.value())) ||
        plan.rulesRevision != hasher_.sha256(PlanSignature::rules(rules.value())) || plan.ruleSetVersion != rules.value().version)
        return Result<void>::failure({"recovery.configuration_changed", "Settings or rules changed; create a fresh plan rather than reuse the approval."});
    const auto report = inspector_.execute(request.planId);
    if (!report) return Result<void>::failure(report.error());
    const auto readiness = request.action == RecoveryAction::Resume ? report.value().resume.readiness : report.value().rollback.readiness;
    const bool finishUndo = request.action == RecoveryAction::Rollback && readiness == RecoveryReadiness::NotApplicable &&
        report.value().interruptedStatus == PlanStatus::RollingBack && report.value().restored > 0;
    if (readiness != RecoveryReadiness::NeedsRevalidation && !finishUndo)
        return Result<void>::failure({readiness == RecoveryReadiness::NeedsStepRecovery ? "recovery.step_resolution_required" : "recovery.choice_blocked",
            "Recovery is blocked or individual transfer evidence needs explicit resolution first; no changes made."});
    // 두 조회 사이에 단계 상태가 바뀌면 원자적 재시작 요청의 비교가 실패하도록 처음 상태를 유지한다.
    RecoveryRestart restart{request, plan.status, {}};
    auto normalized = plan;
    normalized.status = request.action == RecoveryAction::Resume ? PlanStatus::Executing : PlanStatus::RollingBack;
    if (plan.steps.size() != report.value().steps.size()) return Result<void>::failure({"recovery.snapshot_changed", "Step count changed during inspection."});
    for (size_t i = 0; i < plan.steps.size(); ++i) {
        const auto& item = report.value().steps[i];
        if (item.stepId != plan.steps[i].id) return Result<void>::failure({"recovery.snapshot_changed", "Step order changed during inspection."});
        StepStatus restored;
        switch (item.state) {
        case InterruptedStepState::Pending: restored = StepStatus::Planned; break;
        case InterruptedStepState::Moved: restored = StepStatus::Applied; break;
        case InterruptedStepState::Restored: restored = StepStatus::RolledBack; break;
        case InterruptedStepState::Skipped: restored = StepStatus::Skipped; break;
        default: return Result<void>::failure({"recovery.step_unresolved", "A step is still uncertain."});
        }
        restart.steps.push_back({item.stepId, plan.steps[i].status, restored});
        normalized.steps[i].status = restored;
    }
    const auto support = runner_.verifyRecoverySupport(normalized, request.action);
    if (!support) return support;
    const auto recorded = history_.beginRecovery(restart);
    if (!recorded) return recorded;
    // 선택 저장 경계에서도 파일이 바뀔 수 있으므로 실제 실행 전에 다시 관찰하는 함수 흐름.
    const auto rechecked = inspector_.execute(plan.id);
    if (!rechecked) return Result<void>::failure(rechecked.error());
    if (!rechecked.value().planIntact || rechecked.value().needsAttention != 0 ||
        (request.action == RecoveryAction::Resume ? rechecked.value().resume.readiness == RecoveryReadiness::Blocked :
         rechecked.value().rollback.readiness == RecoveryReadiness::Blocked))
        return Result<void>::failure({"recovery.changed_after_choice", "Files or evidence changed while recording the choice; execution stopped."});
    const auto currentSettings = settings_.load();
    const auto currentRules = rules_.load();
    if (!currentSettings || !currentRules ||
        plan.settingsRevision != hasher_.sha256(PlanSignature::settings(currentSettings.value())) ||
        plan.rulesRevision != hasher_.sha256(PlanSignature::rules(currentRules.value())))
        return Result<void>::failure({"recovery.configuration_changed", "Configuration changed while recording the recovery choice."});
    const auto restored = history_.loadPlan(plan.id);
    if (!restored) return Result<void>::failure(restored.error());
    return runner_.runRecoveredPlan(restored.value(), request.action);
}
} // namespace nookbit
