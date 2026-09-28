#include "infrastructure/filesystem/safe_executor.hpp"

#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "modules/planner/path_safety_validator.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace nookbit {
namespace {

// 계획에서 지정된 상태의 파일 단계들을 순서대로 찾는 함수.
std::vector<const MoveStep*> stepsWithStatus(const OperationPlan& plan, StepStatus status) {
    std::vector<const MoveStep*> result;
    for (const auto& step : plan.steps) {
        if (step.status == status) result.push_back(&step);
    }
    return result;
}

} // namespace

// 현재 상태 검증과 이력 저장에 필요한 기능을 받는 생성자.
SafeExecutor::SafeExecutor(ISettingsRepository& settings,
                           IVerifySavedPlanUseCase& verifier,
                           IScanner& scanner,
                           IPlanHasher& hasher,
                           IHistoryRepository& history,
                           std::filesystem::path appDataRoot)
    : settings_(settings), verifier_(&verifier), scanner_(scanner),
      hasher_(hasher), history_(history), appDataRoot_(std::move(appDataRoot)) {}

// 검증 객체 없이 되돌리기만 할 실행기를 만드는 생성자.
SafeExecutor::SafeExecutor(ISettingsRepository& settings,
                           IScanner& scanner,
                           IPlanHasher& hasher,
                           IHistoryRepository& history,
                           std::filesystem::path appDataRoot)
    : settings_(settings), verifier_(nullptr), scanner_(scanner),
      hasher_(hasher), history_(history), appDataRoot_(std::move(appDataRoot)) {}

// 승인된 계획의 파일들을 덮어쓰기 없이 순서대로 실행하는 함수.
Result<void> SafeExecutor::execute(
    const OperationPlan& plan,
    const ApprovalRecord& approval,
    const std::vector<std::filesystem::path>& selectedFiles) {
    if (!verifier_) {
        return Result<void>::failure({
            "execute.verifier_missing", "A verifier is required to move files."});
    }
    OperationMutex operationLock;
    auto locked = operationLock.acquire();
    if (!locked) return locked;
    if (plan.status != PlanStatus::Approved || approval.planId != plan.id ||
        approval.planHash != plan.planHash ||
        approval.approvedAt == std::chrono::system_clock::time_point{}) {
        return Result<void>::failure({"execute.approval_missing", "A matching approval is required."});
    }
    auto settings = settings_.load();
    if (!settings) return Result<void>::failure(settings.error());
    auto checked = verifier_->execute(
        plan.id, selectedFiles, settings.value(), PlanStatus::Approved);
    if (!checked) return Result<void>::failure(checked.error());
    const auto& saved = checked.value();
    if (saved.planHash != plan.planHash ||
        PlanSignature::planContents(saved) != PlanSignature::planContents(plan)) {
        return Result<void>::failure({"execute.plan_mismatch", "Requested plan differs from saved approval."});
    }
    const auto steps = stepsWithStatus(saved, StepStatus::Planned);
    if (steps.empty()) {
        return Result<void>::failure({"execute.no_steps", "There are no files to move."});
    }
    for (const auto* step : steps) {
        if (!step->fingerprint.sha256 ||
            !PlanSignature::isSha256Hex(*step->fingerprint.sha256)) {
            return Result<void>::failure({"execute.content_hash_missing", "Every move needs a content hash."});
        }
        auto safety = PathSafetyValidator::validateMove(
            step->source, step->destination, selectedFiles,
            settings.value().homeRoot, appDataRoot_);
        if (!safety) return safety;
        auto fingerprint = scanFingerprint(scanner_, step->source, &hasher_);
        if (!fingerprint) return Result<void>::failure(fingerprint.error());
        if (!sameFingerprint(step->fingerprint, fingerprint.value())) {
            return Result<void>::failure({"execute.source_changed", "A selected file changed before execution."});
        }
    }

    auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing});
    if (!started) return started;
    std::vector<const MoveStep*> appliedSteps;
    for (const auto* step : steps) {
        auto intent = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::IntentRecorded});
        if (!intent) {
            if (appliedSteps.empty()) {
                auto failed = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
                if (!failed) return failed;
                return intent;
            }
            auto rolled = rollbackAppliedSteps(saved, appliedSteps, PlanStatus::Failed, true);
            if (!rolled) return rolled;
            return Result<void>::failure({
                "execute.batch_rolled_back", "A file could not start; earlier moves were undone.",
            });
        }
        auto moved = WindowsFileMover::moveNoReplace(step->source, step->destination);
        if (!moved) {
            const auto source = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
            const auto destination = probeFile(scanner_, step->destination, step->fingerprint, &hasher_);
            if (source != FileProbe::Matches ||
                (destination != FileProbe::Missing && destination != FileProbe::Different)) {
                auto marked = markNeedsRecovery(plan.id);
                if (!marked) return marked;
                return Result<void>::failure({
                    "execute.needs_recovery", "A move failed with uncertain file state; review is required.",
                });
            }
            auto failedStep = history_.appendEvent(
                StepStatusChanged{plan.id, step->id, StepStatus::Failed});
            if (!failedStep) return failedStep;
            if (appliedSteps.empty()) {
                auto failedPlan = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
                if (!failedPlan) return failedPlan;
                return moved;
            }
            auto rolled = rollbackAppliedSteps(saved, appliedSteps, PlanStatus::Failed, true);
            if (!rolled) return rolled;
            return Result<void>::failure({
                "execute.batch_rolled_back", "A file could not move; earlier moves were undone.",
            });
        }
        auto movedFingerprint = scanFingerprint(scanner_, step->destination, &hasher_);
        if (!movedFingerprint ||
            !sameFingerprint(step->fingerprint, movedFingerprint.value())) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return Result<void>::failure({
                "execute.needs_recovery", "Moved file could not be verified; review is required.",
            });
        }
        auto applied = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::Applied});
        if (!applied) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return Result<void>::failure({
                "execute.needs_recovery", "File moved, but its completion record needs review.",
            });
        }
        appliedSteps.push_back(step);
    }
    auto completed = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Completed});
    if (!completed) {
        return Result<void>::failure({
            "execute.needs_recovery", "File moved, but recording plan completion failed.",
        });
    }
    return Result<void>::success();
}

// 확인 불가 상태를 기록하고 작업을 중단하는 함수.
Result<void> SafeExecutor::markNeedsRecovery(const PlanId& planId) {
    auto loaded = history_.loadPlan(planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    for (const auto& step : loaded.value().steps) {
        if (step.status == StepStatus::Skipped || step.status == StepStatus::NeedsRecovery) continue;
        auto changed = history_.appendEvent(
            StepStatusChanged{planId, step.id, StepStatus::NeedsRecovery});
        if (!changed) return changed;
    }
    return history_.appendEvent(PlanStatusChanged{planId, PlanStatus::NeedsRecovery});
}

// 이미 적용된 단계들을 검증하고 역순으로 되돌려 최종 상태를 기록하는 함수.
Result<void> SafeExecutor::rollbackAppliedSteps(
    const OperationPlan& plan,
    const std::vector<const MoveStep*>& applied,
    PlanStatus finalStatus,
    bool duringExecution) {
    if (applied.empty()) {
        return Result<void>::failure({"rollback.no_steps", "There are no applied files to undo."});
    }
    for (const auto* step : applied) {
        if (!step->fingerprint.sha256 ||
            !PlanSignature::isSha256Hex(*step->fingerprint.sha256) ||
            !step->source.is_absolute() || !step->destination.is_absolute() ||
            !appDataRoot_.is_absolute() ||
            PathSafetyValidator::isWithin(step->source, appDataRoot_) ||
            PathSafetyValidator::isWithin(step->destination, appDataRoot_) ||
            probeFile(scanner_, step->destination, step->fingerprint, &hasher_) != FileProbe::Matches ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Missing) {
            if (duringExecution) {
                auto marked = markNeedsRecovery(plan.id);
                if (!marked) return marked;
            }
            return Result<void>::failure({
                duringExecution ? "execute.rollback_needs_recovery" : "rollback.file_changed",
                "An applied file cannot be safely returned; no file was overwritten.",
            });
        }
    }
    auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::RollingBack});
    if (!started) return started;
    for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
        const auto* step = *iterator;
        if (probeFile(scanner_, step->destination, step->fingerprint, &hasher_) != FileProbe::Matches ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Missing) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return Result<void>::failure({
                "rollback.needs_recovery", "A file changed during undo; review is required.",
            });
        }
        auto intent = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::RollbackIntentRecorded});
        if (!intent) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return intent;
        }
        auto moved = WindowsFileMover::moveNoReplace(step->destination, step->source);
        if (!moved ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Matches ||
            probeFile(scanner_, step->destination, step->fingerprint, &hasher_) != FileProbe::Missing) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return Result<void>::failure({
                "rollback.needs_recovery", "Undo stopped with uncertain file state; review is required.",
            });
        }
        auto reverted = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::RolledBack});
        if (!reverted) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return reverted;
        }
    }
    return history_.appendEvent(PlanStatusChanged{plan.id, finalStatus});
}

// 완료된 계획의 이동 파일들을 역순으로 안전하게 되돌리는 함수.
Result<void> SafeExecutor::rollback(const PlanId& planId) {
    OperationMutex operationLock;
    auto locked = operationLock.acquire();
    if (!locked) return locked;
    auto loaded = history_.loadPlan(planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    const auto& plan = loaded.value();
    if ((plan.status != PlanStatus::Completed && plan.status != PlanStatus::Failed) ||
        !PlanSignature::isSha256Hex(plan.planHash) ||
        plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan))) {
        return Result<void>::failure({
            "rollback.plan_invalid", "Only an intact completed or failed plan can be rolled back.",
        });
    }
    const auto applied = stepsWithStatus(plan, StepStatus::Applied);
    return rollbackAppliedSteps(plan, applied, PlanStatus::RolledBack, false);
}

} // namespace nookbit
