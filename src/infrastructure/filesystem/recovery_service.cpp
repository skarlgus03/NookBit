#include "infrastructure/filesystem/recovery_service.hpp"

#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"

#include <algorithm>

namespace nookbit {
namespace {

// 건너뛴 단계 외에 실제 파일 작업 단계 하나만 찾는 함수.
const MoveStep* onlyActiveStep(const OperationPlan& plan) {
    const MoveStep* active = nullptr;
    for (const auto& step : plan.steps) {
        if (step.status == StepStatus::Skipped) continue;
        if (active) return nullptr;
        active = &step;
    }
    return active;
}

// 출발지와 목적지 중 파일이 한 곳에만 그대로 있는지 확인하는 함수.
bool isOnlyMatch(FileProbe matching, FileProbe missing) {
    return matching == FileProbe::Matches && missing == FileProbe::Missing;
}

// 계획의 서명과 파일 내용 해시가 수동 복구에 충분한지 확인하는 함수.
bool canTrustRecoveryPlan(const OperationPlan& plan, const MoveStep* step,
                          IPlanHasher& hasher) {
    return step && step->fingerprint.sha256 &&
           PlanSignature::isSha256Hex(*step->fingerprint.sha256) &&
           PlanSignature::isSha256Hex(plan.planHash) &&
           plan.planHash == hasher.sha256(PlanSignature::approvalPayload(plan));
}

} // namespace

// 이력과 현재 파일 정보를 확인할 기능을 받는 생성자.
RecoveryService::RecoveryService(IHistoryRepository& history,
                                 IScanner& scanner,
                                 IPlanHasher& hasher)
    : history_(history), scanner_(scanner), hasher_(hasher) {}

// 불확실한 계획을 NeedsRecovery 상태로 기록하는 함수.
Result<void> RecoveryService::markNeedsRecovery(
    const OperationPlan& plan,
    const MoveStep* step) {
    if (step && step->status != StepStatus::NeedsRecovery &&
        step->status != StepStatus::Skipped) {
        auto marked = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::NeedsRecovery});
        if (!marked) return marked;
    }
    return history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::NeedsRecovery});
}

// 파일 이동 없이 확실한 상태만 DB에 반영하고 불확실한 계획은 표시하는 함수.
Result<RecoveryReport> RecoveryService::reconcile() {
    OperationMutex operationLock;
    auto locked = operationLock.acquire();
    if (!locked) return Result<RecoveryReport>::failure(locked.error());
    auto loaded = history_.incompletePlans();
    if (!loaded) return Result<RecoveryReport>::failure(loaded.error());
    RecoveryReport report;
    for (const auto& plan : loaded.value()) {
        if (plan.status == PlanStatus::NeedsRecovery) {
            report.needsAttention.push_back(plan.id);
            continue;
        }
        if (plan.status != PlanStatus::Executing && plan.status != PlanStatus::RollingBack) {
            continue;
        }
        const auto* step = onlyActiveStep(plan);
        const bool intact = canTrustRecoveryPlan(plan, step, hasher_);
        if (!intact) {
            auto marked = markNeedsRecovery(plan, step);
            if (!marked) return Result<RecoveryReport>::failure(marked.error());
            report.needsAttention.push_back(plan.id);
            continue;
        }

        if (plan.status == PlanStatus::Executing && step->status == StepStatus::Planned) {
            auto marked = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
            if (!marked) return Result<RecoveryReport>::failure(marked.error());
            ++report.reconciled;
            continue;
        }
        if (plan.status == PlanStatus::Executing && step->status == StepStatus::Failed) {
            auto marked = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
            if (!marked) return Result<RecoveryReport>::failure(marked.error());
            ++report.reconciled;
            continue;
        }

        const auto source = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
        const auto destination = probeFile(scanner_, step->destination, step->fingerprint, &hasher_);
        if (plan.status == PlanStatus::Executing &&
            step->status == StepStatus::IntentRecorded) {
            if (isOnlyMatch(source, destination)) {
                auto stepFailed = history_.appendEvent(
                    StepStatusChanged{plan.id, step->id, StepStatus::Failed});
                if (!stepFailed) return Result<RecoveryReport>::failure(stepFailed.error());
                auto planFailed = history_.appendEvent(
                    PlanStatusChanged{plan.id, PlanStatus::Failed});
                if (!planFailed) return Result<RecoveryReport>::failure(planFailed.error());
                ++report.reconciled;
                continue;
            }
            if (isOnlyMatch(destination, source)) {
                auto applied = history_.appendEvent(
                    StepStatusChanged{plan.id, step->id, StepStatus::Applied});
                if (!applied) return Result<RecoveryReport>::failure(applied.error());
                auto completed = history_.appendEvent(
                    PlanStatusChanged{plan.id, PlanStatus::Completed});
                if (!completed) return Result<RecoveryReport>::failure(completed.error());
                ++report.reconciled;
                continue;
            }
        }
        if (plan.status == PlanStatus::Executing && step->status == StepStatus::Applied &&
            isOnlyMatch(destination, source)) {
            auto completed = history_.appendEvent(
                PlanStatusChanged{plan.id, PlanStatus::Completed});
            if (!completed) return Result<RecoveryReport>::failure(completed.error());
            ++report.reconciled;
            continue;
        }
        if (plan.status == PlanStatus::RollingBack &&
            step->status == StepStatus::RollbackIntentRecorded &&
            isOnlyMatch(source, destination)) {
            auto rolledStep = history_.appendEvent(
                StepStatusChanged{plan.id, step->id, StepStatus::RolledBack});
            if (!rolledStep) return Result<RecoveryReport>::failure(rolledStep.error());
            auto rolledPlan = history_.appendEvent(
                PlanStatusChanged{plan.id, PlanStatus::RolledBack});
            if (!rolledPlan) return Result<RecoveryReport>::failure(rolledPlan.error());
            ++report.reconciled;
            continue;
        }
        if (plan.status == PlanStatus::RollingBack &&
            step->status == StepStatus::RollbackIntentRecorded &&
            isOnlyMatch(destination, source)) {
            auto restoredStep = history_.appendEvent(
                StepStatusChanged{plan.id, step->id, StepStatus::Applied});
            if (!restoredStep) return Result<RecoveryReport>::failure(restoredStep.error());
            auto restoredPlan = history_.appendEvent(
                PlanStatusChanged{plan.id, PlanStatus::Completed});
            if (!restoredPlan) return Result<RecoveryReport>::failure(restoredPlan.error());
            ++report.reconciled;
            continue;
        }
        if (plan.status == PlanStatus::RollingBack && step->status == StepStatus::Applied &&
            isOnlyMatch(destination, source)) {
            auto restoredPlan = history_.appendEvent(
                PlanStatusChanged{plan.id, PlanStatus::Completed});
            if (!restoredPlan) return Result<RecoveryReport>::failure(restoredPlan.error());
            ++report.reconciled;
            continue;
        }
        if (plan.status == PlanStatus::RollingBack && step->status == StepStatus::RolledBack &&
            isOnlyMatch(source, destination)) {
            auto rolledPlan = history_.appendEvent(
                PlanStatusChanged{plan.id, PlanStatus::RolledBack});
            if (!rolledPlan) return Result<RecoveryReport>::failure(rolledPlan.error());
            ++report.reconciled;
            continue;
        }
        auto marked = markNeedsRecovery(plan, step);
        if (!marked) return Result<RecoveryReport>::failure(marked.error());
        report.needsAttention.push_back(plan.id);
    }
    return Result<RecoveryReport>::success(std::move(report));
}

// 확인 필요 작업의 두 파일 위치와 복구 가능 여부를 읽는 함수.
Result<std::vector<RecoveryCase>> RecoveryService::inspectCases() {
    auto loaded = history_.incompletePlans();
    if (!loaded) return Result<std::vector<RecoveryCase>>::failure(loaded.error());
    std::vector<RecoveryCase> cases;
    for (const auto& plan : loaded.value()) {
        if (plan.status != PlanStatus::NeedsRecovery) continue;
        RecoveryCase item;
        item.planId = plan.id;
        const auto* step = onlyActiveStep(plan);
        if (step) {
            item.source = step->source;
            item.destination = step->destination;
            item.contentHashAvailable = step->fingerprint.sha256 &&
                PlanSignature::isSha256Hex(*step->fingerprint.sha256);
            item.planIntact = canTrustRecoveryPlan(plan, step, hasher_);
            if (item.planIntact) {
                item.sourceState = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
                item.destinationState = probeFile(scanner_, step->destination, step->fingerprint, &hasher_);
            }
        }
        auto origin = history_.interruptedStatus(plan.id);
        item.originKnown = origin && (origin.value() == PlanStatus::Executing ||
                                       origin.value() == PlanStatus::RollingBack);
        item.canResolve = item.planIntact && item.originKnown &&
            (isOnlyMatch(item.sourceState, item.destinationState) ||
             isOnlyMatch(item.destinationState, item.sourceState));
        cases.push_back(std::move(item));
    }
    return Result<std::vector<RecoveryCase>>::success(std::move(cases));
}

// 사용자가 요청한 작업을 다시 검사해 확실한 기록만 정리하는 함수.
Result<void> RecoveryService::resolveCase(const PlanId& planId) {
    OperationMutex operationLock;
    auto locked = operationLock.acquire();
    if (!locked) return locked;
    auto loaded = history_.loadPlan(planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    const auto& plan = loaded.value();
    const auto* step = onlyActiveStep(plan);
    if (plan.status != PlanStatus::NeedsRecovery || !canTrustRecoveryPlan(plan, step, hasher_)) {
        return Result<void>::failure({
            "recovery.plan_untrusted", "The recovery record lacks a verified content hash or intact plan.",
        });
    }
    auto origin = history_.interruptedStatus(planId);
    if (!origin) return Result<void>::failure(origin.error());
    const auto source = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
    const auto destination = probeFile(scanner_, step->destination, step->fingerprint, &hasher_);
    StepStatus stepOutcome;
    PlanStatus planOutcome;
    if (isOnlyMatch(source, destination)) {
        stepOutcome = origin.value() == PlanStatus::RollingBack
            ? StepStatus::RolledBack : StepStatus::Failed;
        planOutcome = origin.value() == PlanStatus::RollingBack
            ? PlanStatus::RolledBack : PlanStatus::Failed;
    } else if (isOnlyMatch(destination, source)) {
        stepOutcome = StepStatus::Applied;
        planOutcome = PlanStatus::Completed;
    } else {
        return Result<void>::failure({
            "recovery.ambiguous", "File locations are still ambiguous; no file or record was changed.",
        });
    }
    if (step->status != stepOutcome) {
        auto updated = history_.appendEvent(StepStatusChanged{planId, step->id, stepOutcome});
        if (!updated) return updated;
    }
    return history_.appendEvent(PlanStatusChanged{planId, planOutcome});
}

} // namespace nookbit
