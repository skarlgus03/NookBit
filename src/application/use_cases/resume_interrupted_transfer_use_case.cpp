#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {
// 작업 조율에 필요한 인터페이스를 빌려 받고 구체 일꾼은 생성하지 않는 함수.
ResumeInterruptedTransferUseCase::ResumeInterruptedTransferUseCase(const IHistoryRepository& history,
    ITransferJournal& journal, IStagedCopy& copy, ITransferSourceRemoval& removal, ITransferFileInspector& files,
    ITransferRecoveryGuard& removalGuard, ITransferCommitRecoveryGuard& commitGuard, const IPlanHasher& hasher)
    : history_(history), journal_(journal), copy_(copy), removal_(removal), files_(files),
      removalGuard_(removalGuard), commitGuard_(commitGuard), hasher_(hasher) {}

// 진단값만으로 파일 변경을 허가하지 않고 현재 승인 및 순수 단계 계약을 검사하는 함수.
Result<void> ResumeInterruptedTransferUseCase::verifyResume(const TransferRecoveryInspection& inspection) {
    const auto& attempt = inspection.attempt;
    if (attempt.direction != TransferDirection::Forward || attempt.phase == TransferPhase::Completed ||
        attempt.stagingCleanup != StagingCleanup::None || !inspection.observationErrors.empty())
        return Result<void>::failure({"recovery.transfer_resume_unsupported", "Only owned unresolved forward transfers without cleanup or observation errors may resume."});
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, attempt);
    if (!binding) return Result<void>::failure(binding.error());
    auto eligible = attempt;
    Result<TransferAttempt> permitted = Result<TransferAttempt>::failure({"recovery.transfer_resume_unsupported", "This finding has no supported forward recovery route."});
    switch (inspection.finding) {
    case TransferRecoveryFinding::PartialCopy:
        if (eligible.copyResume == CopyResume::IntentRecorded) eligible.copyResume = CopyResume::None;
        permitted = TransferValidation::recordCopyResume(eligible, CopyResume::IntentRecorded);
        break;
    case TransferRecoveryFinding::VerifiedStaging:
        if (eligible.copyResume == CopyResume::Verified) {
            if (eligible.stagingCommit == StagingCommit::IntentRecorded) eligible.stagingCommit = StagingCommit::None;
            permitted = TransferValidation::recordStagingCommit(eligible, StagingCommit::IntentRecorded);
        } else {
            // 일반 검증/확정 의도 단계도 원본과 임시 파일이 남아 있으면 중단 기록 후 재검증한다.
            if (eligible.phase == TransferPhase::CopyVerified || eligible.phase == TransferPhase::CommitIntent) eligible.phase = TransferPhase::NeedsRecovery;
            if (eligible.copyResume == CopyResume::IntentRecorded) eligible.copyResume = CopyResume::None;
            permitted = TransferValidation::recordCopyResume(eligible, CopyResume::IntentRecorded);
        }
        break;
    case TransferRecoveryFinding::CommitNotRecorded:
        if (attempt.stagingFingerprint) permitted = TransferValidation::reconcileDestinationCommitted(attempt, *attempt.stagingFingerprint, true);
        break;
    case TransferRecoveryFinding::CommittedSourceRetained:
        eligible.recoveryRemovalIntent = false;
        permitted = TransferValidation::recordRecoveryRemovalIntent(eligible);
        break;
    case TransferRecoveryFinding::SourceAbsentAfterRemoveIntent:
        permitted = TransferValidation::reconcileCompleted(attempt, true);
        break;
    default: break;
    }
    return permitted ? Result<void>::success() : Result<void>::failure(permitted.error());
}

// 각 단계마다 새 증거를 읽고 순서가 진행된 경우에만 다음 기존 유스케이스를 호출하는 함수.
Result<void> ResumeInterruptedTransferUseCase::resume(const std::string& attemptId) {
    InspectTransferRecoveryUseCase inspect(history_, journal_, files_, hasher_);
    for (int boundary = 0; boundary < 4; ++boundary) {
        const auto observed = inspect.execute(attemptId);
        if (!observed) return Result<void>::failure(observed.error());
        const auto& report = observed.value();
        if (report.attempt.phase == TransferPhase::Completed && report.attempt.direction == TransferDirection::Forward &&
            report.finding == TransferRecoveryFinding::SourceAbsentAfterRemoveIntent) return Result<void>::success();
        const auto supported = verifyResume(report);
        if (!supported) return supported;
        Result<TransferAttempt> result = Result<TransferAttempt>::failure({"recovery.transfer_resume_unsupported", "Unsupported recovery boundary."});
        switch (report.finding) {
        case TransferRecoveryFinding::PartialCopy:
        case TransferRecoveryFinding::VerifiedStaging:
            if (report.attempt.copyResume == CopyResume::Verified) {
                CommitResumedStagingUseCase commit(history_, journal_, copy_, hasher_);
                result = commit.execute(attemptId);
            } else {
                if (report.attempt.phase == TransferPhase::CopyVerified || report.attempt.phase == TransferPhase::CommitIntent) {
                    const auto stopped = journal_.advance(attemptId, {report.attempt.phase, TransferPhase::NeedsRecovery, {}, {},
                        Error{"transfer.explicit_staging_resume", "User-selected whole-plan resume requires protected staging revalidation."}});
                    if (!stopped) return stopped;
                }
                ResumePartialCopyUseCase resume(history_, journal_, copy_, hasher_);
                result = resume.execute(attemptId);
            }
            break;
        case TransferRecoveryFinding::CommitNotRecorded: {
            ReconcileTransferCommitUseCase commit(history_, journal_, commitGuard_, hasher_);
            result = commit.execute(attemptId); break;
        }
        case TransferRecoveryFinding::CommittedSourceRetained: {
            ResumeCommittedTransferUseCase finish(history_, journal_, removal_, removalGuard_, hasher_);
            result = finish.execute(attemptId); break;
        }
        case TransferRecoveryFinding::SourceAbsentAfterRemoveIntent: {
            ReconcileTransferRecoveryUseCase finish(history_, journal_, removalGuard_, hasher_);
            result = finish.execute(attemptId); break;
        }
        default: break;
        }
        if (!result) return Result<void>::failure(result.error());
    }
    return Result<void>::failure({"recovery.transfer_progress_invalid", "Forward recovery did not reach a verified completed result in its bounded route."});
}
}
