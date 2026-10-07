#include "infrastructure/filesystem/safe_executor.hpp"

#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/diagnostics/operation_timing.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "modules/planner/path_safety_validator.hpp"

#include <algorithm>
#include <atomic>
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
                           std::filesystem::path appDataRoot, ITransferVolumeInspector* volumes, ITransferStepExecutor* transfers,
                           IRollbackTransferStepExecutor* reversals)
    : settings_(settings), verifier_(&verifier), scanner_(scanner),
      hasher_(hasher), history_(history), appDataRoot_(std::move(appDataRoot)), volumes_(volumes), transfers_(transfers), reversals_(reversals) {}

// 검증 객체 없이 되돌리기만 할 실행기를 만드는 생성자.
SafeExecutor::SafeExecutor(ISettingsRepository& settings,
                           IScanner& scanner,
                           IPlanHasher& hasher,
                           IHistoryRepository& history,
                           std::filesystem::path appDataRoot, ITransferVolumeInspector* volumes, IRollbackTransferStepExecutor* reversals)
    : settings_(settings), verifier_(nullptr), scanner_(scanner),
      hasher_(hasher), history_(history), appDataRoot_(std::move(appDataRoot)), volumes_(volumes), reversals_(reversals) {}

// 복사 전략을 이름 변경으로 대체하지 않고 실제 승인 볼륨을 재확인하는 함수.
Result<void> SafeExecutor::verifyMovement(const OperationPlan& plan, const MoveStep* step) {
    const auto valid = PlanSignature::validateFormat(plan);
    if (!valid) return valid;
    if (plan.formatVersion == 1) return PlanSignature::validateMovementScope(plan);
    if (!volumes_) return Result<void>::failure({"execute.volume_verifier_required", "V2 execution requires volume verification."});
    PrepareTransferPlanUseCase prepare(*volumes_);
    if (!step) return prepare.verify(plan);
    OperationPlan single;
    single.formatVersion = 2;
    single.steps.push_back(*step);
    return prepare.verify(single);
}

// 승인된 계획의 파일들을 덮어쓰기 없이 순서대로 실행하는 함수.
Result<void> SafeExecutor::execute(
    const OperationPlan& plan,
    const ApprovalRecord& approval,
    const std::vector<std::filesystem::path>& selectedFiles) {
    const auto format = PlanSignature::validateFormat(plan);
    if (!format) return format;
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
    TimingStage savedPlanCheck("executor_plan_check");
    auto checked = verifier_->execute(
        plan.id, selectedFiles, settings.value(), PlanStatus::Approved);
    savedPlanCheck.stop();
    if (!checked) return Result<void>::failure(checked.error());
    const auto& saved = checked.value();
    if (PlanSignature::isMetadataRenamePlan(saved)) {
        if (saved.planHash != plan.planHash || PlanSignature::planContents(saved) != PlanSignature::planContents(plan))
            return Result<void>::failure({"execute.plan_mismatch", "Requested metadata plan differs from saved approval."});
        return executeMetadataSteps(saved, selectedFiles, settings.value());
    }
    const bool hasCopy = saved.formatVersion == 2 && std::any_of(saved.steps.begin(), saved.steps.end(), [](const MoveStep& item) {
        return item.status != StepStatus::Skipped && item.movement && item.movement->strategy == MoveStrategy::VerifiedCopy;
    });
    if (hasCopy && (!transfers_ || (!reversals_ && std::count_if(saved.steps.begin(), saved.steps.end(), [](const MoveStep& item) {
        return item.status != StepStatus::Skipped;
    }) > 1))) return Result<void>::failure({"execute.transfer_executor_required", "Copy batches require transfer and reverse-transfer services."});
    const auto movement = verifyMovement(saved);
    if (!movement) return movement;
    if (saved.planHash != plan.planHash ||
        PlanSignature::planContents(saved) != PlanSignature::planContents(plan)) {
        return Result<void>::failure({"execute.plan_mismatch", "Requested plan differs from saved approval."});
    }
    const auto steps = stepsWithStatus(saved, StepStatus::Planned);
    if (steps.empty()) {
        return Result<void>::failure({"execute.no_steps", "There are no files to move."});
    }
    TimingStage batchCheck("batch_source_check");
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

    batchCheck.stop();
    auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing});
    if (!started) return started;
    return executePreparedSteps(saved, steps, {});
}

// 보호된 객체의 이동을 시도하며 확실한 실패는 남기고 불확실한 결과는 중단하는 함수.
Result<void> SafeExecutor::executeMetadataSteps(const OperationPlan& plan,
    const std::vector<std::filesystem::path>& selected, const UserSettings& settings) {
    const auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing});
    if (!started) return started;
    int failures = 0;
    for (const auto& step : plan.steps) {
        if (step.status != StepStatus::Planned) continue;
        const auto intent = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::IntentRecorded});
        if (!intent) { const auto marked = markNeedsRecovery(plan.id); return marked ? intent : marked; }
        auto result = PathSafetyValidator::validateMove(step.source, step.destination, selected, settings.homeRoot, appDataRoot_);
        if (result) result = verifyMovement(plan, &step);
        if (result) result = WindowsFileMover::moveMatchingNoReplace(step.source, step.destination, step.fingerprint);
        if (!result) {
            // 이름 변경 API 실패가 실제 이동 결과를 증명하지 못하면 다음 파일로 진행하지 않는다.
            if (result.error().code == "move.rename_failed" &&
                (probeFile(scanner_, step.source, step.fingerprint) != FileProbe::Matches ||
                 probeFile(scanner_, step.destination, step.fingerprint) == FileProbe::Matches ||
                 probeFile(scanner_, step.destination, step.fingerprint) == FileProbe::Unavailable)) {
                const auto marked = markNeedsRecovery(plan.id);
                return marked ? Result<void>::failure({"execute.needs_recovery", "Rename outcome is uncertain; preserve files and review."}) : marked;
            }
            const auto failed = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Failed, result.error()});
            if (!failed) { const auto marked = markNeedsRecovery(plan.id); return marked ? failed : marked; }
            ++failures; continue;
        }
        if (probeFile(scanner_, step.source, step.fingerprint) != FileProbe::Missing ||
            probeFile(scanner_, step.destination, step.fingerprint) != FileProbe::Matches) {
            const auto marked = markNeedsRecovery(plan.id);
            return marked ? Result<void>::failure({"execute.needs_recovery", "Moved object could not be confirmed; review required."}) : marked;
        }
        const auto applied = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Applied});
        if (!applied) { const auto marked = markNeedsRecovery(plan.id); return marked ? applied : marked; }
    }
    const auto finished = history_.appendEvent(PlanStatusChanged{plan.id, failures ? PlanStatus::Failed : PlanStatus::Completed});
    if (!finished) { const auto marked = markNeedsRecovery(plan.id); return marked ? finished : marked; }
    return failures ? Result<void>::failure({"execute.partial_failure", std::to_string(failures) + " file(s) could not move; successful files remain moved. See per-file history."}) : Result<void>::success();
}

// 정방향 최초 실행과 복구가 동일한 이동 및 보상 경계를 사용하는 함수.
Result<void> SafeExecutor::executePreparedSteps(const OperationPlan& saved,
    const std::vector<const MoveStep*>& steps, std::vector<const MoveStep*> appliedSteps) {
    const auto& plan = saved;
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
        // 앞선 이동이나 의도 기록 중 바뀐 파일은 옮기지 않는 최종 검사.
        const auto currentMovement = verifyMovement(saved, step);
        if (!currentMovement) {
            const auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return currentMovement;
        }
        TimingStage finalSourceCheck("file_source_check", step->fingerprint.size);
        const auto sourceState = probeFile(scanner_, step->source, step->fingerprint, &hasher_);
        finalSourceCheck.stop();
        if (sourceState != FileProbe::Matches) {
            auto failedStep = history_.appendEvent(
                StepStatusChanged{plan.id, step->id, StepStatus::Failed});
            if (!failedStep) return failedStep;
            if (appliedSteps.empty()) {
                auto failedPlan = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
                if (!failedPlan) return failedPlan;
                return Result<void>::failure({
                    "execute.source_changed", "A source changed immediately before its move; it was not moved.",
                });
            }
            auto rolled = rollbackAppliedSteps(saved, appliedSteps, PlanStatus::Failed, true);
            if (!rolled) return rolled;
            return Result<void>::failure({
                "execute.batch_rolled_back", "A source changed; earlier moves were undone and this source was not moved.",
            });
        }
        if (saved.formatVersion == 2 && step->movement->strategy == MoveStrategy::VerifiedCopy) {
            static std::atomic<std::uint64_t> attemptCounter{0};
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            const auto attemptId = "transfer-" + std::to_string(stamp) + "-" + std::to_string(++attemptCounter);
            const auto staging = step->destination.parent_path() / (".nookbit-" + attemptId + ".tmp");
            const auto transferred = transfers_->execute(plan.id, step->id, attemptId, staging);
            if (!transferred) {
                const auto marked = markNeedsRecovery(plan.id);
                if (!marked) return marked;
                return transferred;
            }
            const auto recorded = history_.loadPlan(plan.id);
            if (!recorded) return Result<void>::failure(recorded.error());
            const auto completedStep = std::find_if(recorded.value().steps.begin(), recorded.value().steps.end(), [&](const MoveStep& item) { return item.id == step->id; });
            if (completedStep == recorded.value().steps.end() || completedStep->status != StepStatus::Applied ||
                recorded.value().planHash != saved.planHash ||
                recorded.value().planHash != hasher_.sha256(PlanSignature::approvalPayload(recorded.value()))) {
                const auto marked = markNeedsRecovery(plan.id);
                if (!marked) return marked;
                return Result<void>::failure({"execute.transfer_record_invalid", "Transfer result is not backed by an intact applied-step record."});
            }
            appliedSteps.push_back(step);
            continue;
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
    const auto currentPlan = history_.loadPlan(plan.id);
    if (!currentPlan) return Result<void>::failure(currentPlan.error());
    // 마지막 복사 단계가 이미 원자적으로 전체 완료를 기록했다면 중복하지 않는다.
    if (currentPlan.value().status == PlanStatus::Completed) return Result<void>::success();
    auto completed = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Completed});
    if (!completed) {
        return Result<void>::failure({
            "execute.needs_recovery", "File moved, but recording plan completion failed.",
        });
    }
    return Result<void>::success();
}

// 복구 선택을 기록하기 전에 서비스 구성과 보호 범위를 읽기 전용으로 검사하는 함수.
Result<void> SafeExecutor::verifyRecoverySupport(const OperationPlan& plan, RecoveryAction action) {
    if (std::any_of(plan.steps.begin(), plan.steps.end(), [](const MoveStep& step) {
        return step.movement && step.movement->preservation == PreservationPolicy::LocalNtfsBasicWithZone;
    })) return Result<void>::failure({"recovery.zone_unsupported", "Interrupted download-marker transfer recovery is not yet supported."});
    if ((action != RecoveryAction::Resume && action != RecoveryAction::Rollback) ||
        !PlanSignature::validateFormat(plan) || !PlanSignature::validateMovementScope(plan) ||
        !PlanSignature::isSha256Hex(plan.planHash) || plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan)))
        return Result<void>::failure({"recovery.plan_invalid", "An intact selected recovery plan is required."});
    const auto settings = settings_.load();
    if (!settings) return Result<void>::failure(settings.error());
    if (plan.settingsRevision != hasher_.sha256(PlanSignature::settings(settings.value())))
        return Result<void>::failure({"recovery.settings_changed", "Settings changed before recovery execution."});
    const auto movement = verifyMovement(plan);
    if (!movement) return movement;
    std::vector<std::filesystem::path> selected;
    bool hasCopy = false;
    for (const auto& step : plan.steps) {
        if (step.status == StepStatus::Skipped) continue;
        if ((step.status != StepStatus::Planned && step.status != StepStatus::Applied && step.status != StepStatus::RolledBack) ||
            (action == RecoveryAction::Resume && step.status == StepStatus::RolledBack) ||
            !step.fingerprint.sha256 || !PlanSignature::isSha256Hex(*step.fingerprint.sha256))
            return Result<void>::failure({"recovery.steps_unresolved", "Recovery execution requires normalized, hashed step evidence."});
        selected.push_back(step.source);
        hasCopy = hasCopy || (step.movement && step.movement->strategy == MoveStrategy::VerifiedCopy);
    }
    if (hasCopy && ((action == RecoveryAction::Resume && !transfers_) || !reversals_))
        return Result<void>::failure({"recovery.executor_missing", "Recovery requires the existing forward and reverse transfer services."});
    for (const auto& step : plan.steps) {
        if (step.status == StepStatus::Skipped) continue;
        if (!step.source.is_absolute() || !step.destination.is_absolute() || !appDataRoot_.is_absolute())
            return Result<void>::failure({"recovery.path_invalid", "Recovery paths and application data root must be absolute."});
        const auto safety = PathSafetyValidator::validateMove(step.source, step.destination, selected, settings.value().homeRoot, appDataRoot_);
        if (!safety) return safety;
    }
    return Result<void>::success();
}

// 공유 잠금 아래 복구 기록과 실제 파일을 다시 검사하고 기존 실행 루프를 재사용하는 함수.
Result<void> SafeExecutor::runRecoveredPlan(const OperationPlan& plan, RecoveryAction action) {
    const auto support = verifyRecoverySupport(plan, action);
    if (!support) return support;
    const auto loaded = history_.loadPlan(plan.id);
    if (!loaded) return Result<void>::failure(loaded.error());
    if (plan.status != (action == RecoveryAction::Resume ? PlanStatus::Executing : PlanStatus::RollingBack) ||
        loaded.value().status != plan.status || loaded.value().planHash != plan.planHash ||
        PlanSignature::planContents(loaded.value()) != PlanSignature::planContents(plan))
        return Result<void>::failure({"recovery.restart_changed", "The recorded restart no longer matches the selected recovery."});
    const auto applied = stepsWithStatus(plan, StepStatus::Applied);
    if (action == RecoveryAction::Rollback) {
        if (applied.empty()) return history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::RolledBack});
        return rollbackAppliedSteps(plan, applied, PlanStatus::RolledBack, true);
    }
    const auto pending = stepsWithStatus(plan, StepStatus::Planned);
    // 조회 이후 바뀐 대기 원본을 발견하면 이동 전에 멈추고 중단 상태를 유지한다.
    for (const auto* step : pending) {
        if (probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Matches) {
            const auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return Result<void>::failure({"recovery.source_changed", "A pending source changed immediately before resuming."});
        }
    }
    return executePreparedSteps(plan, pending, applied);
}

// 확인 불가 상태를 기록하고 작업을 중단하는 함수.
Result<void> SafeExecutor::markNeedsRecovery(const PlanId& planId) {
    auto loaded = history_.loadPlan(planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    for (const auto& step : loaded.value().steps) {
        // 미시작·제외·복원 완료의 확정 증거는 불확실한 진행 상태로 덮어쓰지 않는다.
        if (step.status == StepStatus::Planned || step.status == StepStatus::Skipped ||
            step.status == StepStatus::NeedsRecovery || step.status == StepStatus::RolledBack) continue;
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
    const bool hasCopy = plan.formatVersion == 2 && std::any_of(applied.begin(), applied.end(), [](const MoveStep* step) {
        return step->movement && step->movement->strategy == MoveStrategy::VerifiedCopy;
    });
    if (hasCopy && !reversals_) return Result<void>::failure({"rollback.transfer_executor_required", "Copy rollback requires reverse-transfer services."});
    const auto movement = verifyMovement(plan);
    if (!movement) {
        if (duringExecution) {
            const auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
        }
        return movement;
    }
    if (applied.empty()) {
        return Result<void>::failure({"rollback.no_steps", "There are no applied files to undo."});
    }
    for (const auto* step : applied) {
        const bool copyStep = plan.formatVersion == 2 && step->movement->strategy == MoveStrategy::VerifiedCopy;
        Result<void> reverseReady = Result<void>::success();
        if (copyStep) reverseReady = reversals_->verifyRollback(plan.id, step->id);
        if ((!PlanSignature::isMetadataRenamePlan(plan) && (!step->fingerprint.sha256 ||
            !PlanSignature::isSha256Hex(*step->fingerprint.sha256))) ||
            !step->source.is_absolute() || !step->destination.is_absolute() ||
            !appDataRoot_.is_absolute() ||
            PathSafetyValidator::isWithin(step->source, appDataRoot_) ||
            PathSafetyValidator::isWithin(step->destination, appDataRoot_) ||
            !reverseReady || (!copyStep && (probeFile(scanner_, step->destination, step->fingerprint, &hasher_) != FileProbe::Matches ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Missing))) {
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
    if (plan.status != PlanStatus::RollingBack) {
        auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::RollingBack});
        if (!started) return started;
    }
    for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
        const auto* step = *iterator;
        if (plan.formatVersion == 2 && step->movement->strategy == MoveStrategy::VerifiedCopy) {
            static std::atomic<std::uint64_t> reverseCounter{0};
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            const auto attemptId = "rollback-" + std::to_string(stamp) + "-" + std::to_string(++reverseCounter);
            const auto staging = step->source.parent_path() / (".nookbit-" + attemptId + ".tmp");
            const auto reversed = reversals_->rollback(plan.id, step->id, attemptId, staging);
            if (!reversed) {
                const auto marked = markNeedsRecovery(plan.id);
                if (!marked) return marked;
                return reversed;
            }
            continue;
        }
        auto intent = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::RollbackIntentRecorded});
        if (!intent) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return intent;
        }
        // 되돌리기 의도까지 기록한 뒤 실제 이동 직전에 두 위치를 다시 검사한다.
        const auto currentMovement = verifyMovement(plan, step);
        if (!currentMovement) {
            const auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return currentMovement;
        }
        if (probeFile(scanner_, step->destination, step->fingerprint, &hasher_) != FileProbe::Matches ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Missing) {
            auto marked = markNeedsRecovery(plan.id);
            if (!marked) return marked;
            return Result<void>::failure({
                "rollback.needs_recovery", "A file changed during undo; review is required.",
            });
        }
        auto moved = PlanSignature::isMetadataRenamePlan(plan)
            ? WindowsFileMover::moveMatchingNoReplace(step->destination, step->source, step->fingerprint)
            : WindowsFileMover::moveNoReplace(step->destination, step->source);
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
    const auto currentPlan = history_.loadPlan(plan.id);
    if (!currentPlan) return Result<void>::failure(currentPlan.error());
    if (currentPlan.value().status == finalStatus) return Result<void>::success();
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
    const auto format = PlanSignature::validateFormat(plan);
    if (!format) return format;
    if ((plan.status != PlanStatus::Completed && plan.status != PlanStatus::Failed) ||
        !PlanSignature::isSha256Hex(plan.planHash) ||
        plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan))) {
        return Result<void>::failure({
            "rollback.plan_invalid", "Only an intact completed or failed plan can be rolled back.",
        });
    }
    const auto applied = stepsWithStatus(plan, StepStatus::Applied);
    if (plan.formatVersion == 2 && std::any_of(applied.begin(), applied.end(), [](const MoveStep* step) {
        return step->movement->strategy == MoveStrategy::VerifiedCopy;
    })) {
        if (applied.size() != 1 || std::count_if(plan.steps.begin(), plan.steps.end(), [](const MoveStep& step) { return step.status != StepStatus::Skipped; }) != 1)
            return rollbackAppliedSteps(plan, applied, PlanStatus::RolledBack, false);
        if (!reversals_ || !volumes_ || plan.status != PlanStatus::Completed)
            return Result<void>::failure({"rollback.transfer_executor_required", "Approved reverse transfer requires its dedicated executor."});
        const auto* step = applied[0];
        if (!step->source.is_absolute() || !step->destination.is_absolute() || !appDataRoot_.is_absolute() ||
            PathSafetyValidator::isWithin(step->source, appDataRoot_) || PathSafetyValidator::isWithin(step->destination, appDataRoot_))
            return Result<void>::failure({"rollback.path_invalid", "Reverse transfer paths must stay outside application data."});
        PrepareTransferPlanUseCase prepare(*volumes_);
        const auto current = prepare.verify(plan);
        if (!current) return current;
        static std::atomic<std::uint64_t> reverseCounter{0};
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const auto attemptId = "rollback-" + std::to_string(stamp) + "-" + std::to_string(++reverseCounter);
        const auto staging = step->source.parent_path() / (".nookbit-" + attemptId + ".tmp");
        const auto reversed = reversals_->rollback(plan.id, step->id, attemptId, staging);
        if (!reversed) {
            const auto after = history_.loadPlan(plan.id);
            if (!after) return Result<void>::failure(after.error());
            if (after.value().status == PlanStatus::RollingBack) {
                const auto marked = markNeedsRecovery(plan.id);
                if (!marked) return marked;
            }
        }
        // 완료 트랜잭션에서 이미 RolledBack을 기록하므로 중복 기록하지 않는다.
        return reversed;
    }
    return rollbackAppliedSteps(plan, applied, PlanStatus::RolledBack, false);
}

} // namespace nookbit
