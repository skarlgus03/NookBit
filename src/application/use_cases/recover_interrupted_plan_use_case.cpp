#include <nookbit/application/recover_interrupted_plan_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"

namespace nookbit {
// 외부 일꾼을 생성하지 않고 사용자 선택에 필요한 포트만 받는 함수.
RecoverInterruptedPlanUseCase::RecoverInterruptedPlanUseCase(InspectInterruptedPlanUseCase& inspector,
    IHistoryRepository& history, const ISettingsRepository& settings, const IRuleSetRepository& rules,
    const IPlanHasher& hasher, IRecoveredPlanRunner& runner, IInterruptedTransferResolver* transfers)
    : inspector_(inspector), history_(history), settings_(settings), rules_(rules), hasher_(hasher), runner_(runner), transfers_(transfers) {}

// 복구 도중에도 변경된 설정이나 규칙으로 원래 승인을 재사용하지 않는 함수.
Result<void> RecoverInterruptedPlanUseCase::verifyConfiguration(const OperationPlan& plan) const {
    const auto settings = settings_.load();
    if (!settings) return Result<void>::failure(settings.error());
    const auto rules = rules_.load();
    if (!rules) return Result<void>::failure(rules.error());
    if (plan.settingsRevision != hasher_.sha256(PlanSignature::settings(settings.value())) ||
        plan.rulesRevision != hasher_.sha256(PlanSignature::rules(rules.value())) || plan.ruleSetVersion != rules.value().version)
        return Result<void>::failure({"recovery.configuration_changed", "Settings or rules changed; the original approval cannot be reused."});
    return Result<void>::success();
}

// 가변 실행 상태를 제외한 원래 승인 내용과 단계 ID를 계속 대조하는 함수.
Result<void> RecoverInterruptedPlanUseCase::verifyChosenApproval(const OperationPlan& plan) const {
    const auto current = history_.loadPlan(plan.id);
    if (!current) return Result<void>::failure(current.error());
    if (current.value().id != plan.id || current.value().planHash != plan.planHash ||
        current.value().steps.size() != plan.steps.size() ||
        PlanSignature::approvalPayload(current.value()) != PlanSignature::approvalPayload(plan))
        return Result<void>::failure({"recovery.choice_stale", "Approval contents changed after the explicit choice."});
    for (size_t i = 0; i < plan.steps.size(); ++i)
        if (current.value().steps[i].id != plan.steps[i].id)
            return Result<void>::failure({"recovery.choice_stale", "Step identifiers changed after the explicit choice."});
    return Result<void>::success();
}

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
    const auto configuration = verifyConfiguration(plan);
    if (!configuration) return configuration;
    const auto report = inspector_.execute(request.planId);
    if (!report) return Result<void>::failure(report.error());
    const auto readiness = request.action == RecoveryAction::Resume ? report.value().resume.readiness : report.value().rollback.readiness;
    const bool finishUndo = request.action == RecoveryAction::Rollback && readiness == RecoveryReadiness::NotApplicable &&
        report.value().interruptedStatus == PlanStatus::RollingBack && (report.value().restored > 0 || report.value().preserved > 0);
    const bool preservePartial = request.action == RecoveryAction::Rollback && readiness == RecoveryReadiness::NeedsStepRecovery;
    const bool resolveTransfers = request.action == RecoveryAction::Resume && readiness == RecoveryReadiness::NeedsStepRecovery && transfers_;
    if (readiness != RecoveryReadiness::NeedsRevalidation && !finishUndo && !resolveTransfers && !preservePartial)
        return Result<void>::failure({readiness == RecoveryReadiness::NeedsStepRecovery ? "recovery.step_resolution_required" : "recovery.choice_blocked",
            "Recovery is blocked or individual transfer evidence needs explicit resolution first; no changes made."});
    if (preservePartial) for (const auto& item : report.value().steps) {
        if (item.state != InterruptedStepState::NeedsAttention) continue;
        if (!item.transfer || !canPreservePartialTransfer(*item.transfer))
            return Result<void>::failure({"recovery.preserve_unsupported", "Only an intact original with an owned partial stage and absent final may be preserved for undo."});
    }
    if (resolveTransfers) {
        for (const auto& item : report.value().steps) {
            if (item.state != InterruptedStepState::NeedsAttention) continue;
            if (!item.transfer) return Result<void>::failure({"recovery.step_unresolved", "Uncertain step has no tracked transfer."});
            const auto supported = transfers_->verifyResume(*item.transfer);
            if (!supported) return supported;
        }
    }
    // 두 조회 사이에 단계 상태가 바뀌면 원자적 재시작 요청의 비교가 실패하도록 처음 상태를 유지한다.
    RecoveryRestart restart{request, plan.status, {}};
    restart.preserved = report.value().preservedTransfers;
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
        case InterruptedStepState::Preserved: restored = StepStatus::Planned; break;
        case InterruptedStepState::NeedsAttention:
            if (preservePartial) {
                restart.preserved.push_back({item.transfer->attempt.id, item.stepId, item.transfer->attempt.staging});
                restored = StepStatus::Planned;
            } else if (resolveTransfers) restored = StepStatus::IntentRecorded;
            else return Result<void>::failure({"recovery.step_unresolved", "A step is still uncertain."});
            break;
        default: return Result<void>::failure({"recovery.step_unresolved", "A step is still uncertain."});
        }
        restart.steps.push_back({item.stepId, plan.steps[i].status, restored});
        normalized.steps[i].status = restored;
    }
    // 개별 전송은 아직 실행 대상으로 넘기지 않고 서비스 구성·보호 범위만 점검한다.
    auto supportPlan = normalized;
    for (auto& step : supportPlan.steps) if (step.status == StepStatus::IntentRecorded) step.status = StepStatus::Planned;
    const auto support = runner_.verifyRecoverySupport(supportPlan, request.action);
    if (!support) return support;
    const auto recorded = history_.beginRecovery(restart);
    if (!recorded) return recorded;
    const auto chosen = verifyChosenApproval(plan);
    if (!chosen) return chosen;
    // 선택 저장 경계에서도 파일이 바뀔 수 있으므로 실제 실행 전에 다시 관찰하는 함수 흐름.
    const auto rechecked = inspector_.execute(plan.id);
    if (!rechecked) return Result<void>::failure(rechecked.error());
    if (!rechecked.value().planIntact || (!resolveTransfers && rechecked.value().needsAttention != 0) ||
        (request.action == RecoveryAction::Resume ? rechecked.value().resume.readiness == RecoveryReadiness::Blocked :
         rechecked.value().rollback.readiness == RecoveryReadiness::Blocked))
        return Result<void>::failure({"recovery.changed_after_choice", "Files or evidence changed while recording the choice; execution stopped."});
    const auto currentConfiguration = verifyConfiguration(plan);
    if (!currentConfiguration) return currentConfiguration;
    if (resolveTransfers) {
        // 선택 저장 후 바뀐 증거도 모두 점검한 뒤 첫 전송을 복구한다.
        for (const auto& item : rechecked.value().steps) {
            if (item.state != InterruptedStepState::NeedsAttention) continue;
            const auto preflightChoice = verifyChosenApproval(plan);
            if (!preflightChoice) return preflightChoice;
            if (!item.transfer) return Result<void>::failure({"recovery.changed_after_choice", "Tracked transfer evidence disappeared."});
            const auto supported = transfers_->verifyResume(*item.transfer);
            if (!supported) return supported;
        }
        for (const auto& item : rechecked.value().steps) {
            if (item.state != InterruptedStepState::NeedsAttention) continue;
            const auto stillChosen = verifyChosenApproval(plan);
            if (!stillChosen) return stillChosen;
            const auto configured = verifyConfiguration(plan);
            if (!configured) return configured;
            const auto resolved = transfers_->resume(item.transfer->attempt.id);
            if (!resolved) return resolved;
        }
        const auto stillChosen = verifyChosenApproval(plan);
        if (!stillChosen) return stillChosen;
        const auto afterTransfers = inspector_.inspectAfterResume(plan.id);
        if (!afterTransfers) return Result<void>::failure(afterTransfers.error());
        if (!afterTransfers.value().planIntact || afterTransfers.value().needsAttention != 0 ||
            afterTransfers.value().resume.readiness == RecoveryReadiness::Blocked)
            return Result<void>::failure({"recovery.transfer_still_unresolved", "Whole-plan evidence did not become safe after individual transfer recovery."});
        const auto configured = verifyConfiguration(plan);
        if (!configured) return configured;
        if (afterTransfers.value().status == PlanStatus::Completed) {
            if (afterTransfers.value().pending != 0 || afterTransfers.value().restored != 0)
                return Result<void>::failure({"recovery.completion_inconsistent", "Completed recovery contains unfinished or restored steps."});
            return verifyChosenApproval(plan);
        }
    }
    const auto restored = history_.loadPlan(plan.id);
    if (!restored) return Result<void>::failure(restored.error());
    const auto finalChoice = verifyChosenApproval(plan);
    if (!finalChoice) return finalChoice;
    return runner_.runRecoveredPlan(restored.value(), request.action);
}
} // namespace nookbit
