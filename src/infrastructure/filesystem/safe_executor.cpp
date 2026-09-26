#include "infrastructure/filesystem/safe_executor.hpp"

#include "application/use_cases/plan_signature.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "modules/planner/path_safety_validator.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace nookbit {
namespace {

// 실행 대상 중 실제 이동할 단 한 단계를 찾는 함수.
Result<MoveStep> singlePlannedStep(const OperationPlan& plan, StepStatus status) {
    const auto found = std::find_if(plan.steps.begin(), plan.steps.end(),
                                    [status](const MoveStep& step) {
                                        return step.status == status;
                                    });
    if (found == plan.steps.end() ||
        std::count_if(plan.steps.begin(), plan.steps.end(),
                      [status](const MoveStep& step) { return step.status == status; }) != 1) {
        return Result<MoveStep>::failure({
            "execute.single_step_only", "This first executor supports one file operation per plan.",
        });
    }
    return Result<MoveStep>::success(*found);
}

} // namespace

// 현재 상태 검증과 이력 저장에 필요한 기능을 받는 생성자.
SafeExecutor::SafeExecutor(ISettingsRepository& settings,
                           IRuleSetRepository& rules,
                           IScanner& scanner,
                           IPlanner& planner,
                           IPlanHasher& hasher,
                           IHistoryRepository& history,
                           std::filesystem::path appDataRoot)
    : settings_(settings), rules_(rules), scanner_(scanner), planner_(planner),
      hasher_(hasher), history_(history), appDataRoot_(std::move(appDataRoot)) {}

// DB에 승인된 단일 파일 계획을 덮어쓰기 없이 실행하는 함수.
Result<void> SafeExecutor::execute(
    const OperationPlan& plan,
    const ApprovalRecord& approval,
    const std::vector<std::filesystem::path>& selectedFiles) {
    OperationMutex operationLock;
    auto locked = operationLock.acquire();
    if (!locked) return locked;
    if (plan.status != PlanStatus::Approved || approval.planId != plan.id ||
        approval.planHash != plan.planHash ||
        approval.approvedAt == std::chrono::system_clock::time_point{}) {
        return Result<void>::failure({"execute.approval_missing", "A matching approval is required."});
    }
    VerifySavedPlanUseCase verifier(settings_, rules_, scanner_, planner_, hasher_, history_);
    auto checked = verifier.execute(plan.id, selectedFiles, PlanStatus::Approved);
    if (!checked) return Result<void>::failure(checked.error());
    const auto& saved = checked.value();
    if (saved.planHash != plan.planHash ||
        PlanSignature::planContents(saved) != PlanSignature::planContents(plan)) {
        return Result<void>::failure({"execute.plan_mismatch", "Requested plan differs from saved approval."});
    }
    auto step = singlePlannedStep(saved, StepStatus::Planned);
    if (!step) return Result<void>::failure(step.error());
    auto settings = settings_.load();
    if (!settings) return Result<void>::failure(settings.error());
    auto safety = PathSafetyValidator::validateMove(
        step.value().source, step.value().destination, selectedFiles,
        settings.value().homeRoot, appDataRoot_);
    if (!safety) return safety;
    auto fingerprint = scanFingerprint(scanner_, step.value().source, &hasher_);
    if (!fingerprint) return Result<void>::failure(fingerprint.error());
    if (!sameFingerprint(step.value().fingerprint, fingerprint.value())) {
        return Result<void>::failure({"execute.source_changed", "Selected file changed before execution."});
    }

    auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing});
    if (!started) return started;
    auto intent = history_.appendEvent(
        StepStatusChanged{plan.id, step.value().id, StepStatus::IntentRecorded});
    if (!intent) return intent;

    auto moved = WindowsFileMover::moveNoReplace(step.value().source, step.value().destination);
    if (!moved) {
        auto stepFailed = history_.appendEvent(
            StepStatusChanged{plan.id, step.value().id, StepStatus::Failed});
        if (!stepFailed) return stepFailed;
        auto planFailed = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
        if (!planFailed) return planFailed;
        return moved;
    }
    auto movedFingerprint = scanFingerprint(scanner_, step.value().destination, &hasher_);
    if (!movedFingerprint ||
        !sameFingerprint(step.value().fingerprint, movedFingerprint.value())) {
        auto stepRecovery = history_.appendEvent(
            StepStatusChanged{plan.id, step.value().id, StepStatus::NeedsRecovery});
        if (!stepRecovery) return stepRecovery;
        auto planRecovery = history_.appendEvent(
            PlanStatusChanged{plan.id, PlanStatus::NeedsRecovery});
        if (!planRecovery) return planRecovery;
        return Result<void>::failure({
            "execute.destination_changed", "Moved file identity changed; manual recovery is required.",
        });
    }
    auto applied = history_.appendEvent(
        StepStatusChanged{plan.id, step.value().id, StepStatus::Applied});
    if (!applied) {
        return Result<void>::failure({
            "execute.needs_recovery", "File moved, but recording completion failed; inspect the journal.",
        });
    }
    auto completed = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Completed});
    if (!completed) {
        return Result<void>::failure({
            "execute.needs_recovery", "File moved, but recording plan completion failed.",
        });
    }
    return Result<void>::success();
}

// 완료된 단일 파일 계획을 원래 위치로 안전하게 되돌리는 함수.
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
    auto step = singlePlannedStep(plan, StepStatus::Applied);
    if (!step) return Result<void>::failure(step.error());
    if (!step.value().fingerprint.sha256 ||
        !PlanSignature::isSha256Hex(*step.value().fingerprint.sha256)) {
        return Result<void>::failure({
            "rollback.content_hash_missing", "Undo requires a saved file content hash.",
        });
    }
    if (!step.value().source.is_absolute() || !step.value().destination.is_absolute() ||
        !appDataRoot_.is_absolute() ||
        PathSafetyValidator::isWithin(step.value().source, appDataRoot_) ||
        PathSafetyValidator::isWithin(step.value().destination, appDataRoot_)) {
        return Result<void>::failure({
            "rollback.unsafe_path", "Undo would touch an invalid or protected path.",
        });
    }
    auto fingerprint = scanFingerprint(scanner_, step.value().destination, &hasher_);
    if (!fingerprint) return Result<void>::failure(fingerprint.error());
    if (!sameFingerprint(step.value().fingerprint, fingerprint.value())) {
        return Result<void>::failure({"rollback.destination_changed", "Moved file changed after execution."});
    }
    std::error_code sourceError;
    if (std::filesystem::exists(step.value().source, sourceError)) {
        return Result<void>::failure({
            "rollback.source_occupied", "Original path is occupied; undo will not overwrite it.",
        });
    }
    if (sourceError) {
        return Result<void>::failure({
            "rollback.source_unavailable", "Cannot inspect the original path for undo.",
        });
    }
    auto started = history_.appendEvent(PlanStatusChanged{planId, PlanStatus::RollingBack});
    if (!started) return started;
    auto intent = history_.appendEvent(
        StepStatusChanged{planId, step.value().id, StepStatus::RollbackIntentRecorded});
    if (!intent) return intent;
    auto moved = WindowsFileMover::moveNoReplace(step.value().destination, step.value().source);
    if (!moved) {
        return Result<void>::failure({
            "rollback.needs_recovery", "Undo did not complete; inspect the journal before retrying.",
        });
    }
    auto returnedFingerprint = scanFingerprint(scanner_, step.value().source, &hasher_);
    if (!returnedFingerprint ||
        !sameFingerprint(step.value().fingerprint, returnedFingerprint.value())) {
        auto stepRecovery = history_.appendEvent(
            StepStatusChanged{planId, step.value().id, StepStatus::NeedsRecovery});
        if (!stepRecovery) return stepRecovery;
        auto planRecovery = history_.appendEvent(
            PlanStatusChanged{planId, PlanStatus::NeedsRecovery});
        if (!planRecovery) return planRecovery;
        return Result<void>::failure({
            "rollback.source_changed", "Returned file identity changed; manual recovery is required.",
        });
    }
    auto reverted = history_.appendEvent(
        StepStatusChanged{planId, step.value().id, StepStatus::RolledBack});
    if (!reverted) {
        return Result<void>::failure({
            "rollback.needs_recovery", "File returned, but recording undo failed.",
        });
    }
    auto finished = history_.appendEvent(PlanStatusChanged{planId, PlanStatus::RolledBack});
    if (!finished) return finished;
    return Result<void>::success();
}

} // namespace nookbit
