#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"

#include <algorithm>
#include <set>

namespace nookbit {
namespace {
// 미승인 초안이나 정상 완료 작업을 중단 작업으로 오인하지 않는 함수.
bool interrupted(PlanStatus status) {
    return status == PlanStatus::Executing || status == PlanStatus::RollingBack || status == PlanStatus::NeedsRecovery;
}

// 조회된 객체·내용·수정 시각을 모두 승인 증거와 비교하는 함수.
bool matches(const TransferFileObservation& actual, const FileFingerprint& expected) {
    return actual.presence == TransferPathPresence::Present && actual.volumeId == expected.volumeId &&
        actual.fingerprint && TransferValidation::sameObject(expected, *actual.fingerprint) &&
        TransferValidation::sameContent(expected, *actual.fingerprint) &&
        expected.modifiedUtcMs == actual.fingerprint->modifiedUtcMs;
}

// 올바른 볼륨에서 조회한 부재만 인정하는 함수.
bool absent(const TransferFileObservation& actual, const std::optional<std::string>& volume) {
    return actual.presence == TransferPathPresence::Missing && volume && actual.volumeId == volume;
}

// 각 파일의 진단 상태를 전체 작업 개수에 합산하는 함수.
void count(InterruptedPlanInspection& report, InterruptedStepState state) {
    switch (state) {
    case InterruptedStepState::Pending: ++report.pending; break;
    case InterruptedStepState::Moved: ++report.moved; break;
    case InterruptedStepState::Restored: ++report.restored; break;
    case InterruptedStepState::Skipped: ++report.skipped; break;
    case InterruptedStepState::NeedsAttention: ++report.needsAttention; break;
    case InterruptedStepState::Preserved: ++report.preserved; break;
    }
}

} // namespace

// 원본은 온전하고 최종 파일은 없으며 소유한 부분 임시 객체만 남은 경우를 구분하는 함수.
bool canPreservePartialTransfer(const TransferRecoveryInspection& inspection) {
    const auto& attempt = inspection.attempt;
    return inspection.finding == TransferRecoveryFinding::PartialCopy &&
        inspection.source == TransferRecoveryFileState::Matches && inspection.staging == TransferRecoveryFileState::ObjectOnly &&
        inspection.destination == TransferRecoveryFileState::Missing && inspection.observationErrors.empty() &&
        attempt.direction == TransferDirection::Forward && attempt.stagingFingerprint && !attempt.destinationFingerprint &&
        attempt.stagingCleanup == StagingCleanup::None && attempt.stagingCommit == StagingCommit::None &&
        attempt.copyResume != CopyResume::Verified && !attempt.recoveryRemovalIntent && !attempt.recoveryCompleted &&
        (attempt.phase == TransferPhase::StagingCreated || attempt.phase == TransferPhase::Cancelled ||
         attempt.phase == TransferPhase::Failed || attempt.phase == TransferPhase::NeedsRecovery);
}

namespace {
// 불확실한 파일 상태에 설명을 붙이되 기록이나 파일은 변경하지 않는 함수.
void block(InterruptedStepInspection& item, const char* code, const char* message) {
    item.state = InterruptedStepState::NeedsAttention;
    item.reasons.push_back({code, message});
}

// 기록이 없는 단계의 두 위치를 검사하고 실제 객체 증거로 상태를 구분하는 함수.
void inspectUntracked(const MoveStep& step, bool copy, PlanStatus origin,
    ITransferFileInspector& files, InterruptedStepInspection& item) {
    const bool withZone = step.movement && step.movement->preservation == PreservationPolicy::LocalNtfsBasicWithZone;
    const auto source = withZone ? files.inspectWithZone(step.source) : files.inspect(step.source);
    const auto destination = withZone ? files.inspectWithZone(step.destination) : files.inspect(step.destination);
    const auto targetVolume = step.movement ? std::optional<std::string>{step.movement->destinationVolume.serial} : step.fingerprint.volumeId;
    if (source.error) item.reasons.push_back(*source.error);
    if (destination.error) item.reasons.push_back(*destination.error);
    if (matches(source, step.fingerprint) && absent(destination, targetVolume)) {
        if (!copy && origin == PlanStatus::RollingBack && step.status == StepStatus::NeedsRecovery) {
            block(item, "recovery.rename_origin_uncertain", "File is at its original location, but the records do not distinguish pending from restored; review first.");
            return;
        }
        if (step.status == StepStatus::RolledBack || (!copy && origin == PlanStatus::RollingBack && step.status == StepStatus::RollbackIntentRecorded))
            item.state = InterruptedStepState::Restored;
        else if (step.status == StepStatus::Planned || step.status == StepStatus::IntentRecorded || step.status == StepStatus::NeedsRecovery)
            item.state = InterruptedStepState::Pending;
        else block(item, "recovery.step_record_mismatch", "Source remains but its recorded step outcome is inconsistent.");
    } else if (!copy && absent(source, step.fingerprint.volumeId) && matches(destination, step.fingerprint) &&
        (step.status == StepStatus::IntentRecorded || step.status == StepStatus::Applied ||
         step.status == StepStatus::NeedsRecovery || step.status == StepStatus::RollbackIntentRecorded)) {
        item.state = InterruptedStepState::Moved;
    } else block(item, "recovery.file_state_uncertain", "Locations, ownership, contents or volumes do not prove a safe outcome; preserve all files.");
}

// 기존 세 위치 검사 결과를 전체 작업 상태로 분류하며 부분 복사는 완료로 세지 않는 함수.
bool classifyTransfer(const TransferRecoveryInspection& transfer, InterruptedStepInspection& item) {
    using Finding = TransferRecoveryFinding;
    const auto finding = transfer.finding;
    if (transfer.attempt.phase == TransferPhase::Completed && finding == Finding::SourceAbsentAfterRemoveIntent) {
        item.state = transfer.attempt.direction == TransferDirection::Forward ? InterruptedStepState::Moved : InterruptedStepState::Restored;
        return false;
    }
    item.state = InterruptedStepState::NeedsAttention;
    item.reasons.push_back({"recovery.transfer_review", transfer.reason});
    item.reasons.insert(item.reasons.end(), transfer.observationErrors.begin(), transfer.observationErrors.end());
    return finding == Finding::UntrustedRecord || finding == Finding::ChangedFile ||
        finding == Finding::Unavailable || finding == Finding::Ambiguous;
}
}

// 외부에서 소유한 조회 기능만 빌려 받는 생성자.
InspectInterruptedPlanUseCase::InspectInterruptedPlanUseCase(const IHistoryRepository& history,
    const ITransferJournal& journal, ITransferFileInspector& files, const IPlanHasher& hasher,
    ITransferVolumeInspector* volumes)
    : history_(history), journal_(journal), files_(files), hasher_(hasher), volumes_(volumes) {}

// 파일이나 기록을 고치지 않고 중단된 전체 계획을 검사하는 함수.
Result<InterruptedPlanInspection> InspectInterruptedPlanUseCase::execute(const PlanId& planId) {
    return inspectPlan(planId, false);
}

// 정방향 재개 직후 자동 완료된 계획의 파일 상태도 같은 증거 기준으로 검사하는 함수.
Result<InterruptedPlanInspection> InspectInterruptedPlanUseCase::inspectAfterResume(const PlanId& planId) {
    return inspectPlan(planId, true);
}

// 명시적 재개 후 완료 검사를 제외하면 기존 중단 작업만 검사하는 함수.
Result<InterruptedPlanInspection> InspectInterruptedPlanUseCase::inspectPlan(const PlanId& planId, bool allowCompleted, bool allowPreserved) {
    using Return = Result<InterruptedPlanInspection>;
    const auto loaded = history_.loadPlan(planId);
    if (!loaded) return Return::failure(loaded.error());
    const auto& plan = loaded.value();
    const auto preserved = history_.preservedTransfers(planId);
    if (!preserved) return Return::failure(preserved.error());
    if (!interrupted(plan.status) && !(allowCompleted && plan.status == PlanStatus::Completed) &&
        !(allowPreserved && plan.status == PlanStatus::RolledBack && !preserved.value().empty()))
        return Return::failure({"recovery.not_interrupted", "Only interrupted operations need this inspection."});
    InterruptedPlanInspection report;
    report.planId = plan.id; report.status = plan.status;
    report.preservedTransfers = preserved.value();
    bool unsafe = false;
    std::set<StepId> ids;
    report.planIntact = plan.id == planId && !plan.steps.empty() && PlanSignature::validateFormat(plan) &&
        PlanSignature::validateMovementScope(plan) && PlanSignature::isSha256Hex(plan.planHash) &&
        plan.planHash == hasher_.sha256(PlanSignature::approvalPayload(plan));
    for (const auto& step : plan.steps) {
        if (step.id.empty() || !ids.insert(step.id).second || !step.source.is_absolute() || !step.destination.is_absolute() ||
            step.source.lexically_normal() == step.destination.lexically_normal() ||
            (step.status != StepStatus::Skipped && ((!PlanSignature::isMetadataRenamePlan(plan) && (!step.fingerprint.sha256 ||
             !PlanSignature::isSha256Hex(*step.fingerprint.sha256))) || !step.fingerprint.volumeId || !step.fingerprint.fileId)))
            report.planIntact = false;
    }
    if (plan.status == PlanStatus::NeedsRecovery) {
        const auto origin = history_.interruptedStatus(plan.id);
        if (origin && (origin.value() == PlanStatus::Executing || origin.value() == PlanStatus::RollingBack)) report.interruptedStatus = origin.value();
    } else report.interruptedStatus = plan.status == PlanStatus::Completed ? PlanStatus::Executing :
        plan.status == PlanStatus::RolledBack ? PlanStatus::RollingBack : plan.status;
    std::vector<Error> globalReasons;
    if (std::any_of(plan.steps.begin(), plan.steps.end(), [](const MoveStep& step) { return step.fingerprint.directory; }))
        globalReasons.push_back({"recovery.folder_manual_review", "Interrupted folder moves require manual review. Preserve the source, destination and .nookbit-folder temporary copy; automatic resume is disabled."});
    if (PlanSignature::isMetadataRenamePlan(plan)) globalReasons.push_back({"recovery.metadata_unsupported", "Interrupted metadata-only renames require manual review; automatic resume/rollback is not yet supported."});
    if (!report.planIntact) globalReasons.push_back({"recovery.plan_untrusted", "Plan signature, paths or fingerprint evidence are invalid; files were not inspected."});
    if (!report.interruptedStatus) globalReasons.push_back({"recovery.origin_unknown", "The interrupted execution direction is unknown."});
    if (report.planIntact && plan.formatVersion == 2 && !PlanSignature::isMetadataRenamePlan(plan)) {
        if (!volumes_) globalReasons.push_back({"recovery.volume_verifier_required", "V2 inspection requires current volume identities."});
        else {
            PrepareTransferPlanUseCase prepare(*volumes_);
            const auto checked = prepare.verify(plan);
            if (!checked) globalReasons.push_back(checked.error());
        }
    }
    unsafe = !globalReasons.empty();
    InspectTransferRecoveryUseCase inspect(history_, journal_, files_, hasher_);
    for (const auto& step : plan.steps) {
        InterruptedStepInspection item;
        item.stepId = step.id; item.source = step.source; item.destination = step.destination;
        if (!globalReasons.empty()) item.reasons = globalReasons;
        else if (step.status == StepStatus::Skipped) item.state = InterruptedStepState::Skipped;
        else {
            const bool copy = plan.formatVersion == 2 && step.movement->strategy == MoveStrategy::VerifiedCopy;
            const auto loadedAttempts = journal_.forStep(step.id);
            if (!loadedAttempts) return Return::failure(loadedAttempts.error());
            std::vector<TransferAttempt> forward, reverse;
            const bool preservationRecorded = std::any_of(report.preservedTransfers.begin(), report.preservedTransfers.end(),
                [&](const PreservedTransfer& entry) { return entry.stepId == step.id; });
            for (const auto& attempt : loadedAttempts.value()) {
                if (attempt.planId != plan.id) continue;
                (attempt.direction == TransferDirection::Forward ? forward : reverse).push_back(attempt);
            }
            if ((!copy && (!forward.empty() || !reverse.empty())) || forward.size() > 1 || reverse.size() > 1 ||
                (forward.empty() && !reverse.empty())) {
                block(item, "recovery.attempt_ambiguous", "Transfer records do not identify one safe forward/reverse pair.");
                unsafe = true;
            } else if (forward.empty()) {
                if (preservationRecorded) block(item, "recovery.preservation_untrusted", "Preserved transfer record no longer has its owned journal evidence.");
                else inspectUntracked(step, copy, *report.interruptedStatus, files_, item);
                if (item.state == InterruptedStepState::NeedsAttention) unsafe = true;
            } else {
                const auto forwardReport = inspect.execute(forward[0].id);
                if (!forwardReport) return Return::failure(forwardReport.error());
                item.transfer = forwardReport.value();
                const bool retained = std::any_of(report.preservedTransfers.begin(), report.preservedTransfers.end(), [&](const PreservedTransfer& entry) {
                    return entry.stepId == step.id && entry.attemptId == forward[0].id && entry.staging == forward[0].staging;
                });
                const auto cleanup = history_.preservedCleanup(plan.id, forward[0].id);
                if (!cleanup) return Return::failure(cleanup.error());
                if (retained && cleanup.value() && cleanup.value()->removed && reverse.empty() && report.interruptedStatus == PlanStatus::RollingBack &&
                    item.transfer->source == TransferRecoveryFileState::Matches && item.transfer->staging == TransferRecoveryFileState::Missing &&
                    item.transfer->destination == TransferRecoveryFileState::Missing && item.transfer->observationErrors.empty()) {
                    item.state = InterruptedStepState::Pending;
                    item.reasons.push_back({"recovery.staging_cleaned", "Preserved partial staging was explicitly cleaned; original was not moved."});
                } else if (retained && reverse.empty() && report.interruptedStatus == PlanStatus::RollingBack && canPreservePartialTransfer(*item.transfer)) {
                    item.state = InterruptedStepState::Preserved;
                    item.reasons.push_back({"recovery.staging_preserved", "Original and partial staging were left untouched; staging still needs separate review."});
                } else if (preservationRecorded) {
                    block(item, "recovery.preservation_changed", "Preserved transfer no longer matches its recorded ownership or original file state.");
                    unsafe = true;
                } else if (!reverse.empty()) {
                    if (forwardReport.value().finding == TransferRecoveryFinding::UntrustedRecord || forward[0].phase != TransferPhase::Completed ||
                        !forward[0].destinationFingerprint || !TransferValidation::sameObject(*forward[0].destinationFingerprint, reverse[0].sourceFingerprint) ||
                        !TransferValidation::sameContent(*forward[0].destinationFingerprint, reverse[0].sourceFingerprint) ||
                        forward[0].destinationFingerprint->modifiedUtcMs != reverse[0].sourceFingerprint.modifiedUtcMs) {
                        block(item, "recovery.reverse_origin_untrusted", "Reverse evidence does not belong to a completed forward result.");
                        unsafe = true;
                    } else {
                        const auto reverseReport = inspect.execute(reverse[0].id);
                        if (!reverseReport) return Return::failure(reverseReport.error());
                        item.transfer = reverseReport.value();
                        unsafe = classifyTransfer(*item.transfer, item) || unsafe;
                    }
                } else if (step.status == StepStatus::RolledBack || step.status == StepStatus::Planned) {
                    block(item, "recovery.transfer_step_mismatch", "Tracked forward transfer conflicts with the recorded step state.");
                    unsafe = true;
                } else unsafe = classifyTransfer(*item.transfer, item) || unsafe;
            }
        }
        count(report, item.state);
        report.steps.push_back(std::move(item));
    }
    report.resume.reasons = globalReasons; report.rollback.reasons = globalReasons;
    for (const auto& item : report.steps) if (item.state == InterruptedStepState::NeedsAttention) {
        report.resume.reasons.insert(report.resume.reasons.end(), item.reasons.begin(), item.reasons.end());
        report.rollback.reasons.insert(report.rollback.reasons.end(), item.reasons.begin(), item.reasons.end());
    }
    const auto readiness = unsafe ? RecoveryReadiness::Blocked : report.needsAttention ?
        RecoveryReadiness::NeedsStepRecovery : RecoveryReadiness::NeedsRevalidation;
    report.resume.readiness = readiness; report.rollback.readiness = readiness;
    if (report.interruptedStatus == PlanStatus::RollingBack || report.restored > 0 || !report.preservedTransfers.empty()) {
        report.resume.readiness = RecoveryReadiness::Blocked;
        report.resume.reasons.push_back({"recovery.undo_in_progress", "Undo has begun; do not silently switch back to forward execution."});
    }
    if (!unsafe && report.needsAttention == 0) {
        if (report.pending == 0 && report.moved == 0 && report.interruptedStatus == PlanStatus::Executing && report.preservedTransfers.empty())
            report.resume.readiness = RecoveryReadiness::NotApplicable;
        if (report.moved == 0) report.rollback.readiness = RecoveryReadiness::NotApplicable;
        if (report.pending == 0 && report.moved > 0 && report.interruptedStatus == PlanStatus::Executing)
            report.resume.reasons.push_back({"recovery.completion_record_pending", "All files appear moved; completion records still require explicit revalidation."});
    }
    // 후보는 승인으로 취급하지 않으며 실제 실행·원자적 기록 정리는 별도 사용자 요청이 필요하다.
    return Return::success(std::move(report));
}

// 앱 시작 시 실제 중단 상태만 읽고 미승인·완료 작업은 제외하는 함수.
Result<std::vector<InterruptedPlanInspection>> InspectInterruptedPlanUseCase::inspectAll() {
    const auto plans = history_.incompletePlans();
    if (!plans) return Result<std::vector<InterruptedPlanInspection>>::failure(plans.error());
    std::vector<InterruptedPlanInspection> reports;
    std::set<PlanId> inspected;
    for (const auto& plan : plans.value()) {
        if (!interrupted(plan.status)) continue;
        const auto report = execute(plan.id);
        if (!report && report.error().code == "recovery.not_interrupted") continue;
        if (!report) return Result<std::vector<InterruptedPlanInspection>>::failure(report.error());
        reports.push_back(report.value());
        inspected.insert(plan.id);
    }
    const auto retained = history_.preservedTransferPlans();
    if (!retained) return Result<std::vector<InterruptedPlanInspection>>::failure(retained.error());
    for (const auto& id : retained.value()) {
        if (inspected.contains(id)) continue;
        const auto report = inspectPlan(id, false, true);
        if (!report && report.error().code == "recovery.not_interrupted") continue;
        if (!report) return Result<std::vector<InterruptedPlanInspection>>::failure(report.error());
        reports.push_back(report.value());
    }
    return Result<std::vector<InterruptedPlanInspection>>::success(std::move(reports));
}
} // namespace nookbit
