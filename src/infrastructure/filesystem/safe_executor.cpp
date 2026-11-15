#include "infrastructure/filesystem/safe_executor.hpp"

#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/diagnostics/operation_timing.hpp"
#include <nookbit/application/operation_progress.hpp>
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "infrastructure/filesystem/windows_directory_mover.hpp"
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

// 정방향 이동과 되돌리기의 전송 시도 ID 및 임시 파일 경로를 만드는 함수.
std::pair<std::string, std::filesystem::path> newTransferAttempt(
    const char* prefix, const std::filesystem::path& destination) {
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    auto id = std::string(prefix) + "-" + std::to_string(stamp) + "-" + std::to_string(++counter);
    auto staging = destination.parent_path() / (".nookbit-" + id + ".tmp");
    return {std::move(id), std::move(staging)};
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
    OperationProgressScope::begin(OperationPhase::CheckingPlan);
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
    const bool sourceContentAtCopyOpen = PlanSignature::isFreshVerifiedCopyPlan(plan) &&
        transfers_ && transfers_->verifiesSourceBeforeStaging();
    auto checked = sourceContentAtCopyOpen ? verifier_->executeForProtectedCopy(plan.id, selectedFiles, settings.value()) :
        verifier_->execute(plan.id, selectedFiles, settings.value(), PlanStatus::Approved);
    savedPlanCheck.stop();
    if (!checked) return Result<void>::failure(checked.error());
    const auto& saved = checked.value();
    if (PlanSignature::isMetadataRenamePlan(saved)) {
        if (saved.planHash != plan.planHash || PlanSignature::planContents(saved) != PlanSignature::planContents(plan))
            return Result<void>::failure({"execute.plan_mismatch", "Requested metadata plan differs from saved approval."});
        return executeMetadataSteps(saved, selectedFiles, settings.value());
    }
    const bool hasCopy = saved.formatVersion == 2 && std::any_of(saved.steps.begin(), saved.steps.end(), [](const MoveStep& item) {
        return !item.fingerprint.directory && item.status != StepStatus::Skipped && item.movement && item.movement->strategy == MoveStrategy::VerifiedCopy;
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
    OperationProgressScope::phase(OperationPhase::CheckingSource);
    for (const auto* step : steps) {
        if (!step->fingerprint.sha256 ||
            !PlanSignature::isSha256Hex(*step->fingerprint.sha256)) {
            return Result<void>::failure({"execute.content_hash_missing", "Every move needs a content hash."});
        }
        auto safety = PathSafetyValidator::validateMove(
            step->source, step->destination, selectedFiles,
            settings.value().homeRoot, appDataRoot_, settings.value().allowedDestinationRoots);
        if (!safety) return safety;
        auto fingerprint = scanFingerprint(scanner_, step->source, sourceContentAtCopyOpen ? nullptr : &hasher_);
        if (!fingerprint) return Result<void>::failure(fingerprint.error());
        auto expected = step->fingerprint;
        if (sourceContentAtCopyOpen) expected.sha256.reset();
        if (!sameFingerprint(expected, fingerprint.value())) {
            return Result<void>::failure({"execute.source_changed", "A selected file changed before execution."});
        }
    }

    batchCheck.stop();
    auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing});
    if (!started) return started;
    return executePreparedSteps(saved, steps, {}, sourceContentAtCopyOpen);
}

// 보호된 객체의 이동을 시도하며 확실한 실패는 남기고 불확실한 결과는 중단하는 함수.
Result<void> SafeExecutor::executeMetadataSteps(const OperationPlan& plan,
    const std::vector<std::filesystem::path>& selected, const UserSettings& settings) {
    const auto fileCount = stepsWithStatus(plan, StepStatus::Planned).size();
    OperationProgressScope::begin(OperationPhase::CheckingSource, fileCount);
    const auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing});
    if (!started) return started;
    int failures = 0;
    std::size_t fileIndex = 0;
    for (const auto& step : plan.steps) {
        if (step.status != StepStatus::Planned) continue;
        OperationProgressScope::file(++fileIndex, fileCount, step.source);
        OperationProgressScope::phase(OperationPhase::SavingHistory);
        const auto intent = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::IntentRecorded});
        if (!intent) return Result<void>::failure(recoveryError(plan.id, intent.error()));
        OperationProgressScope::phase(OperationPhase::CheckingSource);
        auto result = PathSafetyValidator::validateMove(step.source, step.destination, selected,
            settings.homeRoot, appDataRoot_, settings.allowedDestinationRoots);
        if (result) result = verifyMovement(plan, &step);
        if (result) {
            OperationProgressScope::phase(OperationPhase::Moving);
            result = WindowsFileMover::moveMatchingNoReplace(step.source, step.destination, step.fingerprint);
        }
        if (!result) {
            // 이름 변경 API 실패가 실제 이동 결과를 증명하지 못하면 다음 파일로 진행하지 않는다.
            if (result.error().code == "move.rename_failed") {
                const auto source = probeFile(scanner_, step.source, step.fingerprint);
                const auto destination = source == FileProbe::Matches
                    ? probeFile(scanner_, step.destination, step.fingerprint)
                    : FileProbe::Unavailable;
                if (destination != FileProbe::Missing && destination != FileProbe::Different) {
                    return Result<void>::failure(recoveryError(plan.id, {
                        "execute.needs_recovery", "Rename outcome is uncertain; preserve files and review.",
                    }));
                }
            }
            const auto failed = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Failed, result.error()});
            if (!failed) return Result<void>::failure(recoveryError(plan.id, failed.error()));
            ++failures;
            continue;
        }
        OperationProgressScope::phase(OperationPhase::CheckingResult);
        if (probeFile(scanner_, step.source, step.fingerprint) != FileProbe::Missing ||
            probeFile(scanner_, step.destination, step.fingerprint) != FileProbe::Matches) {
            return Result<void>::failure(recoveryError(plan.id, {
                "execute.needs_recovery", "Moved object could not be confirmed; review required.",
            }));
        }
        OperationProgressScope::phase(OperationPhase::SavingHistory);
        const auto applied = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Applied});
        if (!applied) return Result<void>::failure(recoveryError(plan.id, applied.error()));
        OperationProgressScope::fileCompleted();
    }
    const auto finished = history_.appendEvent(PlanStatusChanged{plan.id, failures ? PlanStatus::Failed : PlanStatus::Completed});
    if (!finished) return Result<void>::failure(recoveryError(plan.id, finished.error()));
    if (failures) {
        return Result<void>::failure({"execute.partial_failure",
            std::to_string(failures) + " file(s) could not move; successful files remain moved. See per-file history."});
    }
    return Result<void>::success();
}

// 다른 볼륨으로 파일을 옮기고 승인된 계획에 완료가 기록됐는지 확인하는 함수.
Result<void> SafeExecutor::executeVerifiedCopy(const OperationPlan& plan, const MoveStep& step) {
    const auto [attemptId, staging] = newTransferAttempt("transfer", step.destination);
    const auto transferred = transfers_->execute(plan.id, step.id, attemptId, staging);
    if (!transferred) {
        if (transferred.error().code == "transfer.cancelled") {
            // Skipped는 승인 당시의 제외 상태이므로 실행 중에는 취소 사유를 기록한다.
            const auto stopped = history_.appendEvent(StepStatusChanged{
                plan.id, step.id, StepStatus::Failed, transferred.error()
            });
            if (!stopped) return Result<void>::failure(recoveryError(plan.id, stopped.error()));
            return cancelPreparedBatch(plan, transferred.error());
        }
        return Result<void>::failure(recoveryError(plan.id, transferred.error()));
    }

    const auto recorded = history_.loadPlan(plan.id);
    if (!recorded) return Result<void>::failure(recorded.error());
    const auto& recordedPlan = recorded.value();
    const auto completed = std::find_if(recordedPlan.steps.begin(), recordedPlan.steps.end(),
        [&](const MoveStep& item) { return item.id == step.id; });
    if (completed == recordedPlan.steps.end() || completed->status != StepStatus::Applied ||
        recordedPlan.planHash != plan.planHash ||
        recordedPlan.planHash != hasher_.sha256(PlanSignature::approvalPayload(recordedPlan))) {
        return Result<void>::failure(recoveryError(plan.id, {
            "execute.transfer_record_invalid", "Transfer result is not backed by an intact applied-step record.",
        }));
    }
    return Result<void>::success();
}

// 폴더 실패가 확실하면 개별 기록을 남기고, 불확실하면 복구 확인으로 넘기는 함수.
Result<bool> SafeExecutor::executeDirectoryMove(const OperationPlan& plan, const MoveStep& step) {
    const bool copy = step.movement && step.movement->strategy == MoveStrategy::VerifiedCopy;
    const auto moved = copy
        ? WindowsDirectoryMover::move(step.source, step.destination, step.fingerprint, scanner_, hasher_)
        : WindowsFileMover::moveNoReplace(step.source, step.destination);
    if (!moved) {
        const auto source = probeFile(scanner_, step.source, step.fingerprint, &hasher_);
        const auto destination = probeFile(scanner_, step.destination, step.fingerprint, &hasher_);
        if (source != FileProbe::Matches || destination == FileProbe::Matches || destination == FileProbe::Unavailable ||
            moved.error().code == "folder.cleanup_needs_review" || moved.error().code.starts_with("folder.committed_")) {
            return Result<bool>::failure(recoveryError(plan.id, moved.error()));
        }
        const auto failed = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Failed, moved.error()});
        if (!failed) return Result<bool>::failure(failed.error());
        return Result<bool>::success(false);
    }
    if (probeFile(scanner_, step.source, step.fingerprint, &hasher_) != FileProbe::Missing ||
        probeFile(scanner_, step.destination, step.fingerprint, &hasher_) != FileProbe::Matches) {
        return Result<bool>::failure(recoveryError(plan.id, {
            "folder.needs_review", "Folder move needs manual review; copies were preserved.",
        }));
    }
    const auto applied = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Applied});
    if (!applied) return Result<bool>::failure(recoveryError(plan.id, applied.error()));
    return Result<bool>::success(true);
}

// 실패한 묶음을 종료하거나 이미 옮긴 항목을 원래 위치로 되돌리는 함수.
Result<void> SafeExecutor::stopPreparedBatch(const OperationPlan& plan,
    const std::vector<const MoveStep*>& applied, const Error& error, const char* rolledBackMessage) {
    if (applied.empty()) {
        const auto failed = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Failed});
        return failed ? Result<void>::failure(error) : failed;
    }
    const auto rolled = rollbackAppliedSteps(plan, applied, PlanStatus::Failed, true);
    return rolled ? Result<void>::failure({"execute.batch_rolled_back", rolledBackMessage}) : rolled;
}

// 파일 이름 변경 결과와 내용 지문을 확인하고 완료를 기록하는 함수.
Result<void> SafeExecutor::executeCheckedRename(const OperationPlan& plan, const MoveStep& step,
    const std::vector<const MoveStep*>& appliedSteps) {
    OperationProgressScope::phase(OperationPhase::Moving);
    const auto moved = WindowsFileMover::moveNoReplace(step.source, step.destination);
    if (!moved) {
        const auto source = probeFile(scanner_, step.source, step.fingerprint, &hasher_);
        const auto destination = probeFile(scanner_, step.destination, step.fingerprint, &hasher_);
        if (source != FileProbe::Matches ||
            (destination != FileProbe::Missing && destination != FileProbe::Different)) {
            return Result<void>::failure(recoveryError(plan.id, {
                "execute.needs_recovery", "A move failed with uncertain file state; review is required.",
            }));
        }
        const auto failed = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Failed});
        if (!failed) return failed;
        return stopPreparedBatch(plan, appliedSteps, moved.error(),
            "A file could not move; earlier moves were undone.");
    }
    OperationProgressScope::phase(OperationPhase::CheckingResult);
    const auto fingerprint = scanFingerprint(scanner_, step.destination, &hasher_);
    if (!fingerprint || !sameFingerprint(step.fingerprint, fingerprint.value())) {
        return Result<void>::failure(recoveryError(plan.id, {
            "execute.needs_recovery", "Moved file could not be verified; review is required.",
        }));
    }
    OperationProgressScope::phase(OperationPhase::SavingHistory);
    const auto applied = history_.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::Applied});
    if (!applied) {
        return Result<void>::failure(recoveryError(plan.id, {
            "execute.needs_recovery", "File moved, but its completion record needs review.",
        }));
    }
    return Result<void>::success();
}

// 취소가 확정되면 완료된 이동 기록을 보존하는 함수.
Result<void> SafeExecutor::cancelPreparedBatch(const OperationPlan& plan, const Error& error) {
    const auto stopped = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Cancelled});
    if (!stopped) return Result<void>::failure(recoveryError(plan.id, stopped.error()));
    return Result<void>::failure(error);
}

// 정방향 최초 실행과 복구가 동일한 이동 및 보상 경계를 사용하는 함수.
Result<void> SafeExecutor::executePreparedSteps(const OperationPlan& plan,
    const std::vector<const MoveStep*>& steps, std::vector<const MoveStep*> appliedSteps,
    bool sourceContentAtCopyOpen) {
    OperationProgressScope::begin(OperationPhase::CheckingSource, steps.size());
    int folderFailures = 0;
    std::size_t fileIndex = 0;
    for (const auto* step : steps) {
        if (OperationProgressScope::cancellationRequested()) {
            return cancelPreparedBatch(plan, {
                "transfer.cancelled", "Stopped before starting the next file.",
            });
        }
        OperationProgressScope::file(++fileIndex, steps.size(), step->source);
        OperationProgressScope::phase(OperationPhase::SavingHistory);
        const auto intent = history_.appendEvent(StepStatusChanged{plan.id, step->id, StepStatus::IntentRecorded});
        if (!intent) {
            return stopPreparedBatch(plan, appliedSteps, intent.error(),
                "A file could not start; earlier moves were undone.");
        }

        // 앞선 이동이나 의도 기록 중 바뀐 파일은 옮기지 않는 최종 검사.
        OperationProgressScope::phase(OperationPhase::CheckingSource);
        const auto currentMovement = verifyMovement(plan, step);
        if (!currentMovement) return Result<void>::failure(recoveryError(plan.id, currentMovement.error()));

        TimingStage finalSourceCheck("file_source_check", step->fingerprint.size);
        auto expected = step->fingerprint;
        if (sourceContentAtCopyOpen) expected.sha256.reset();
        const auto sourceState = probeFile(scanner_, step->source, expected, sourceContentAtCopyOpen ? nullptr : &hasher_);
        finalSourceCheck.stop();
        if (sourceState != FileProbe::Matches) {
            const auto failed = history_.appendEvent(StepStatusChanged{plan.id, step->id, StepStatus::Failed});
            if (!failed) return failed;
            return stopPreparedBatch(plan, appliedSteps,
                {"execute.source_changed", "A source changed immediately before its move; it was not moved."},
                "A source changed; earlier moves were undone and this source was not moved.");
        }

        if (step->fingerprint.directory) {
            const auto moved = executeDirectoryMove(plan, *step);
            if (!moved) return Result<void>::failure(moved.error());
            if (!moved.value()) {
                ++folderFailures;
                continue;
            }
        } else {
            const auto moved = plan.formatVersion == 2 && step->movement->strategy == MoveStrategy::VerifiedCopy
                ? executeVerifiedCopy(plan, *step)
                : executeCheckedRename(plan, *step, appliedSteps);
            if (!moved) return moved;
        }
        appliedSteps.push_back(step);
        OperationProgressScope::fileCompleted();
    }
    const auto currentPlan = history_.loadPlan(plan.id);
    if (!currentPlan) return Result<void>::failure(currentPlan.error());
    // 마지막 복사 단계가 이미 원자적으로 전체 완료를 기록했다면 중복하지 않는다.
    if (currentPlan.value().status == PlanStatus::Completed) return Result<void>::success();
    auto completed = history_.appendEvent(PlanStatusChanged{plan.id, folderFailures ? PlanStatus::Failed : PlanStatus::Completed});
    if (!completed) {
        return Result<void>::failure({
            "execute.needs_recovery", "File moved, but recording plan completion failed.",
        });
    }
    return folderFailures ? Result<void>::failure({"execute.partial_failure", "Some folders could not move; see per-item history."}) : Result<void>::success();
}

// 복구 선택을 기록하기 전에 서비스 구성과 보호 범위를 읽기 전용으로 검사하는 함수.
Result<void> SafeExecutor::verifyRecoverySupport(const OperationPlan& plan, RecoveryAction action) {
    if (std::any_of(plan.steps.begin(), plan.steps.end(), [](const MoveStep& step) { return step.fingerprint.directory; }))
        return Result<void>::failure({"recovery.folder_manual_review", "Interrupted folder moves require manual review; automatic restart is disabled."});
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
        const auto safety = PathSafetyValidator::validateMove(step.source, step.destination, selected, settings.value().homeRoot, appDataRoot_, settings.value().allowedDestinationRoots);
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
            return Result<void>::failure(recoveryError(plan.id, {
                "recovery.source_changed", "A pending source changed immediately before resuming.",
            }));
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

// 원래 오류와 복구 상태 기록 오류 중 호출자에게 돌려줄 오류를 정하는 함수.
Error SafeExecutor::recoveryError(const PlanId& planId, Error error) {
    const auto marked = markNeedsRecovery(planId);
    if (!marked) return marked.error();
    return error;
}

// 되돌리기 대상 폴더에 임시 복사 경로를 지정해 역방향 전송을 요청하는 함수.
Result<void> SafeExecutor::reverseVerifiedCopy(const OperationPlan& plan, const MoveStep& step) {
    const auto [attemptId, staging] = newTransferAttempt("rollback", step.source);
    return reversals_->rollback(plan.id, step.id, attemptId, staging);
}

// 이미 적용된 단계들을 검증하고 역순으로 되돌려 최종 상태를 기록하는 함수.
Result<void> SafeExecutor::rollbackAppliedSteps(
    const OperationPlan& plan,
    const std::vector<const MoveStep*>& applied,
    PlanStatus finalStatus,
    bool duringExecution) {
    OperationProgressScope::begin(OperationPhase::Restoring, applied.size());
    const bool hasCopy = plan.formatVersion == 2 && std::any_of(applied.begin(), applied.end(), [](const MoveStep* step) {
        return !step->fingerprint.directory && step->movement && step->movement->strategy == MoveStrategy::VerifiedCopy;
    });
    if (hasCopy && !reversals_) return Result<void>::failure({"rollback.transfer_executor_required", "Copy rollback requires reverse-transfer services."});
    const auto movement = verifyMovement(plan);
    if (!movement)
        return duringExecution ? Result<void>::failure(recoveryError(plan.id, movement.error())) : movement;
    if (applied.empty()) {
        return Result<void>::failure({"rollback.no_steps", "There are no applied files to undo."});
    }
    for (const auto* step : applied) {
        const bool copyStep = !step->fingerprint.directory && plan.formatVersion == 2 && step->movement->strategy == MoveStrategy::VerifiedCopy;
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
            Error error{
                duringExecution ? "execute.rollback_needs_recovery" : "rollback.file_changed",
                "An applied file cannot be safely returned; no file was overwritten.",
            };
            if (duringExecution) error = recoveryError(plan.id, std::move(error));
            return Result<void>::failure(std::move(error));
        }
    }
    if (plan.status != PlanStatus::RollingBack) {
        auto started = history_.appendEvent(PlanStatusChanged{plan.id, PlanStatus::RollingBack});
        if (!started) return started;
    }
    std::size_t fileIndex = 0;
    for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
        const auto* step = *iterator;
        OperationProgressScope::file(++fileIndex, applied.size(), step->destination);
        OperationProgressScope::phase(OperationPhase::Restoring);
        if (!step->fingerprint.directory && plan.formatVersion == 2 && step->movement->strategy == MoveStrategy::VerifiedCopy) {
            const auto reversed = reverseVerifiedCopy(plan, *step);
            if (!reversed) return Result<void>::failure(recoveryError(plan.id, reversed.error()));
            OperationProgressScope::fileCompleted();
            continue;
        }
        auto intent = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::RollbackIntentRecorded});
        if (!intent) return Result<void>::failure(recoveryError(plan.id, intent.error()));
        // 되돌리기 의도까지 기록한 뒤 실제 이동 직전에 두 위치를 다시 검사한다.
        const auto currentMovement = verifyMovement(plan, step);
        if (!currentMovement) return Result<void>::failure(recoveryError(plan.id, currentMovement.error()));
        const bool copyFolder = step->fingerprint.directory && step->movement &&
            step->movement->strategy == MoveStrategy::VerifiedCopy;
        FileFingerprint verifiedFolder;
        if (probeFile(scanner_, step->destination, step->fingerprint, &hasher_, copyFolder ? &verifiedFolder : nullptr) != FileProbe::Matches ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Missing) {
            return Result<void>::failure(recoveryError(plan.id, {
                "rollback.needs_recovery", "A file changed during undo; review is required.",
            }));
        }
        // 복사 실행기는 방금 검증한 폴더 지문을 받고 보호 핸들 아래에서 내용을 다시 확인한다.
        auto moved = copyFolder
            ? WindowsDirectoryMover::move(step->destination, step->source, verifiedFolder, scanner_, hasher_)
            : PlanSignature::isMetadataRenamePlan(plan)
            ? WindowsFileMover::moveMatchingNoReplace(step->destination, step->source, step->fingerprint)
            : WindowsFileMover::moveNoReplace(step->destination, step->source);
        if (!moved ||
            probeFile(scanner_, step->source, step->fingerprint, &hasher_) != FileProbe::Matches ||
            probeFile(scanner_, step->destination, step->fingerprint, &hasher_) != FileProbe::Missing) {
            return Result<void>::failure(recoveryError(plan.id, {
                "rollback.needs_recovery", "Undo stopped with uncertain file state; review is required.",
            }));
        }
        auto reverted = history_.appendEvent(
            StepStatusChanged{plan.id, step->id, StepStatus::RolledBack});
        if (!reverted) return Result<void>::failure(recoveryError(plan.id, reverted.error()));
        OperationProgressScope::fileCompleted();
    }
    const auto currentPlan = history_.loadPlan(plan.id);
    if (!currentPlan) return Result<void>::failure(currentPlan.error());
    if (currentPlan.value().status == finalStatus) return Result<void>::success();
    return history_.appendEvent(PlanStatusChanged{plan.id, finalStatus});
}

// 완료된 계획의 이동 파일들을 역순으로 안전하게 되돌리는 함수.
Result<void> SafeExecutor::rollback(const PlanId& planId) {
    OperationProgressScope::begin(OperationPhase::Restoring);
    OperationMutex operationLock;
    auto locked = operationLock.acquire();
    if (!locked) return locked;
    auto loaded = history_.loadPlan(planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    const auto& plan = loaded.value();
    const auto format = PlanSignature::validateFormat(plan);
    if (!format) return format;
    if ((plan.status != PlanStatus::Completed && plan.status != PlanStatus::Failed && plan.status != PlanStatus::Cancelled) ||
        !PlanSignature::isSha256Hex(plan.planHash) ||
        plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan))) {
        return Result<void>::failure({
            "rollback.plan_invalid", "Only an intact finished plan can be rolled back.",
        });
    }
    const auto applied = stepsWithStatus(plan, StepStatus::Applied);
    if (plan.formatVersion == 2 && std::any_of(applied.begin(), applied.end(), [](const MoveStep* step) {
        return !step->fingerprint.directory && step->movement->strategy == MoveStrategy::VerifiedCopy;
    })) {
        if (applied.size() != 1 || std::count_if(plan.steps.begin(), plan.steps.end(), [](const MoveStep& step) { return step.status != StepStatus::Skipped; }) != 1)
            return rollbackAppliedSteps(plan, applied, PlanStatus::RolledBack, false);
        if (!reversals_ || !volumes_ || plan.status != PlanStatus::Completed)
            return Result<void>::failure({"rollback.transfer_executor_required", "Approved reverse transfer requires its dedicated executor."});
        const auto* step = applied[0];
        OperationProgressScope::file(1, 1, step->destination);
        if (!step->source.is_absolute() || !step->destination.is_absolute() || !appDataRoot_.is_absolute() ||
            PathSafetyValidator::isWithin(step->source, appDataRoot_) || PathSafetyValidator::isWithin(step->destination, appDataRoot_))
            return Result<void>::failure({"rollback.path_invalid", "Reverse transfer paths must stay outside application data."});
        PrepareTransferPlanUseCase prepare(*volumes_);
        const auto current = prepare.verify(plan);
        if (!current) return current;
        const auto reversed = reverseVerifiedCopy(plan, *step);
        if (!reversed) {
            const auto after = history_.loadPlan(plan.id);
            if (!after) return Result<void>::failure(after.error());
            if (after.value().status == PlanStatus::RollingBack)
                return Result<void>::failure(recoveryError(plan.id, reversed.error()));
        }
        // 완료 트랜잭션에서 이미 RolledBack을 기록하므로 중복 기록하지 않는다.
        if (reversed) OperationProgressScope::fileCompleted();
        return reversed;
    }
    return rollbackAppliedSteps(plan, applied, PlanStatus::RolledBack, false);
}

} // namespace nookbit
