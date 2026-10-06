#include "infrastructure/filesystem/recovery_service.hpp"

#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"

#include <algorithm>

namespace nookbit {
namespace {

// 건너뛴 단계를 제외한 파일 작업 단계들을 찾는 함수.
std::vector<const MoveStep*> activeSteps(const OperationPlan& plan) {
    std::vector<const MoveStep*> active;
    for (const auto& step : plan.steps) {
        if (step.status == StepStatus::Skipped) continue;
        active.push_back(&step);
    }
    return active;
}

// 출발지와 목적지 중 파일이 한 곳에만 그대로 있는지 확인하는 함수.
bool isOnlyMatch(FileProbe matching, FileProbe missing) {
    return matching == FileProbe::Matches && missing == FileProbe::Missing;
}

// 계획의 서명과 파일 내용 해시가 수동 복구에 충분한지 확인하는 함수.
bool canTrustRecoveryPlan(const OperationPlan& plan, IPlanHasher& hasher) {
    const auto active = activeSteps(plan);
    return plan.formatVersion == 1 && PlanSignature::validateFormat(plan) && !active.empty() &&
           std::all_of(active.begin(), active.end(), [](const MoveStep* step) {
               return step->fingerprint.sha256 &&
                      PlanSignature::isSha256Hex(*step->fingerprint.sha256);
           }) &&
           PlanSignature::isSha256Hex(plan.planHash) &&
           plan.planHash == hasher.sha256(PlanSignature::approvalPayload(plan));
}

} // namespace

// 이력과 현재 파일 정보를 확인할 기능을 받는 생성자.
RecoveryService::RecoveryService(IHistoryRepository& history,
                                 IScanner& scanner,
                                 IPlanHasher& hasher, const ITransferJournal* transfers)
    : history_(history), scanner_(scanner), hasher_(hasher), transfers_(transfers) {}

// 전송 시도가 하나라도 있으면 기존 이름 변경 복구로 완료/실패를 추론하지 않는 함수.
Result<bool> RecoveryService::hasTransfers(const OperationPlan& plan) const {
    if (!transfers_) return Result<bool>::success(false);
    for (const auto& step : plan.steps) {
        const auto attempts = transfers_->forStep(step.id);
        if (!attempts) return Result<bool>::failure(attempts.error());
        if (!attempts.value().empty()) return Result<bool>::success(true);
    }
    return Result<bool>::success(false);
}

// 불확실한 계획을 NeedsRecovery 상태로 기록하는 함수.
Result<void> RecoveryService::markNeedsRecovery(const OperationPlan& plan) {
    for (const auto* step : activeSteps(plan)) {
        if (step->status == StepStatus::NeedsRecovery) continue;
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
        const auto tracked = hasTransfers(plan);
        if (!tracked) return Result<RecoveryReport>::failure(tracked.error());
        if (tracked.value()) {
            report.needsAttention.push_back(plan.id);
            continue;
        }
        if (plan.status == PlanStatus::NeedsRecovery) {
            report.needsAttention.push_back(plan.id);
            continue;
        }
        if (plan.status != PlanStatus::Executing && plan.status != PlanStatus::RollingBack) {
            continue;
        }
        // v2의 단계 증거는 새 전체 복구에서 해석하며 구형 시작 복구는 기록도 변경하지 않는다.
        if (plan.formatVersion != 1) {
            report.needsAttention.push_back(plan.id);
            continue;
        }
        const auto active = activeSteps(plan);
        const bool intact = active.size() == 1 && canTrustRecoveryPlan(plan, hasher_);
        if (!intact) {
            auto marked = markNeedsRecovery(plan);
            if (!marked) return Result<RecoveryReport>::failure(marked.error());
            report.needsAttention.push_back(plan.id);
            continue;
        }
        const auto* step = active.front();

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
        auto marked = markNeedsRecovery(plan);
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
        const auto tracked = hasTransfers(plan);
        if (!tracked) return Result<std::vector<RecoveryCase>>::failure(tracked.error());
        const bool modernInterrupted = plan.formatVersion != 1 &&
            (plan.status == PlanStatus::Executing || plan.status == PlanStatus::RollingBack);
        if (plan.status != PlanStatus::NeedsRecovery && !tracked.value() && !modernInterrupted) continue;
        const auto active = activeSteps(plan);
        if (active.empty()) {
            RecoveryCase item;
            item.requiresWholeRecovery = plan.formatVersion != 1;
            item.planId = plan.id;
            item.partIndex = 1;
            item.partCount = 1;
            item.transferTracked = tracked.value();
            cases.push_back(std::move(item));
            continue;
        }
        const bool intact = canTrustRecoveryPlan(plan, hasher_);
        auto origin = history_.interruptedStatus(plan.id);
        const bool originKnown = origin &&
            (origin.value() == PlanStatus::Executing || origin.value() == PlanStatus::RollingBack);
        bool allAtSource = !active.empty();
        bool allAtDestination = !active.empty();
        const auto firstIndex = cases.size();
        for (std::size_t index = 0; index < active.size(); ++index) {
            const auto* step = active[index];
            RecoveryCase item;
            item.requiresWholeRecovery = plan.formatVersion != 1;
            item.planId = plan.id;
            item.partIndex = index + 1;
            item.partCount = active.size();
            item.source = step->source;
            item.destination = step->destination;
            item.contentHashAvailable = step->fingerprint.sha256 &&
                PlanSignature::isSha256Hex(*step->fingerprint.sha256);
            item.planIntact = intact;
            item.originKnown = originKnown;
            item.transferTracked = tracked.value();
            if (intact && !tracked.value()) {
                item.sourceState = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
                item.destinationState = probeFile(scanner_, step->destination, step->fingerprint, &hasher_);
            }
            allAtSource = allAtSource && isOnlyMatch(item.sourceState, item.destinationState);
            allAtDestination = allAtDestination && isOnlyMatch(item.destinationState, item.sourceState);
            cases.push_back(std::move(item));
        }
        for (std::size_t index = firstIndex; index < cases.size(); ++index) {
            cases[index].canResolve = !tracked.value() && intact && originKnown &&
                (allAtSource || allAtDestination);
        }
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
    const auto tracked = hasTransfers(plan);
    if (!tracked) return Result<void>::failure(tracked.error());
    if (tracked.value()) return Result<void>::failure({"recovery.transfer_tracked",
        "Three-location transfer inspection is required; legacy record reconciliation is disabled."});
    const auto active = activeSteps(plan);
    if (plan.status != PlanStatus::NeedsRecovery ||
        !canTrustRecoveryPlan(plan, hasher_)) {
        return Result<void>::failure({
            "recovery.plan_untrusted", "The recovery record lacks a verified content hash or intact plan.",
        });
    }
    auto origin = history_.interruptedStatus(planId);
    if (!origin) return Result<void>::failure(origin.error());
    bool allAtSource = true;
    bool allAtDestination = true;
    for (const auto* step : active) {
        const auto source = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
        const auto destination = probeFile(scanner_, step->destination, step->fingerprint, &hasher_);
        allAtSource = allAtSource && isOnlyMatch(source, destination);
        allAtDestination = allAtDestination && isOnlyMatch(destination, source);
    }
    StepStatus stepOutcome;
    PlanStatus planOutcome;
    if (allAtSource) {
        stepOutcome = origin.value() == PlanStatus::RollingBack
            ? StepStatus::RolledBack : StepStatus::Failed;
        planOutcome = origin.value() == PlanStatus::RollingBack
            ? PlanStatus::RolledBack : PlanStatus::Failed;
    } else if (allAtDestination) {
        stepOutcome = StepStatus::Applied;
        planOutcome = PlanStatus::Completed;
    } else {
        return Result<void>::failure({
            "recovery.ambiguous", "File locations are still ambiguous; no file or record was changed.",
        });
    }
    for (const auto* step : active) {
        if (step->status == stepOutcome) continue;
        if (step->status != StepStatus::NeedsRecovery) {
            auto pending = history_.appendEvent(
                StepStatusChanged{planId, step->id, StepStatus::NeedsRecovery});
            if (!pending) return pending;
        }
        auto updated = history_.appendEvent(StepStatusChanged{planId, step->id, stepOutcome});
        if (!updated) return updated;
    }
    return history_.appendEvent(PlanStatusChanged{planId, planOutcome});
}

} // namespace nookbit
