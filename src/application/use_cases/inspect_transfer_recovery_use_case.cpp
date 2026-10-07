#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <algorithm>
#include <cwctype>

namespace nookbit {
namespace {
// Windows의 대소문자와 경로 구분자 차이를 정규화해 비교하는 함수.
bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
#ifdef _WIN32
    auto a = left.lexically_normal().wstring(), b = right.lexically_normal().wstring();
    for (auto& ch : a) ch = static_cast<wchar_t>(std::towlower(ch));
    for (auto& ch : b) ch = static_cast<wchar_t>(std::towlower(ch));
    return a == b;
#else
    return left.lexically_normal() == right.lexically_normal();
#endif
}

// 같은 내용만으로 다른 객체를 앱의 복사본이라고 판단하지 않는 함수.
TransferRecoveryFileState classify(const TransferFileObservation& observed,
    const std::optional<FileFingerprint>& expected) {
    using State = TransferRecoveryFileState;
    if (observed.presence == TransferPathPresence::Unavailable || !observed.volumeId) return State::Unavailable;
    if (expected && expected->volumeId != observed.volumeId) return State::OtherVolume;
    if (observed.presence == TransferPathPresence::Missing) return State::Missing;
    if (observed.presence != TransferPathPresence::Present || !observed.fingerprint) return State::Unavailable;
    if (!expected || !TransferValidation::sameObject(*expected, *observed.fingerprint)) return State::Unowned;
    if (!expected->sha256) return State::ObjectOnly;
    return TransferValidation::sameContent(*expected, *observed.fingerprint) &&
        expected->modifiedUtcMs == observed.fingerprint->modifiedUtcMs ? State::Matches : State::Changed;
}

// 서명된 원래 계획의 경로·내용과 전송 시도의 연결을 검사하는 함수.
bool trusted(const OperationPlan& plan, const TransferAttempt& attempt, const IPlanHasher& hasher) {
    auto initial = attempt;
    initial.phase = TransferPhase::Prepared;
    initial.removalProof.reset(); initial.sourceRemovalIntentRecorded = false;
    initial.stagingFingerprint.reset();
    initial.destinationFingerprint.reset();
    initial.error.reset();
    initial.recoveryCompleted = false;
    initial.copyResume = CopyResume::None;
    initial.stagingCommit = StagingCommit::None;
    initial.recoveryRemovalIntent = false;
    initial.stagingCleanup = StagingCleanup::None;
    if (!PlanSignature::validateFormat(plan) || !TransferValidation::validateNew(initial) || plan.id != attempt.planId ||
        !PlanSignature::isSha256Hex(plan.planHash) ||
        plan.planHash != hasher.sha256(PlanSignature::approvalPayload(plan))) return false;
    const auto step = std::find_if(plan.steps.begin(), plan.steps.end(), [&](const MoveStep& item) {
        return item.id == attempt.stepId;
    });
    if (step == plan.steps.end() || !TransferValidation::sameContent(step->fingerprint, attempt.sourceFingerprint)) return false;
    if (attempt.direction == TransferDirection::Rollback)
        return samePath(step->destination, attempt.source) && samePath(step->source, attempt.destination);
    return samePath(step->source, attempt.source) && samePath(step->destination, attempt.destination) &&
        TransferValidation::sameObject(step->fingerprint, attempt.sourceFingerprint) &&
        step->fingerprint.modifiedUtcMs == attempt.sourceFingerprint.modifiedUtcMs;
}
} // namespace

// 외부에서 소유하는 이력·저널·파일 검사·해시 포트를 빌려 받는 함수.
InspectTransferRecoveryUseCase::InspectTransferRecoveryUseCase(const IHistoryRepository& history,
    const ITransferJournal& journal, ITransferFileInspector& files, const IPlanHasher& hasher)
    : history_(history), journal_(journal), files_(files), hasher_(hasher) {}

// 파일이나 기록을 수정하지 않고 중단 전송의 관찰 결과를 반환하는 함수.
Result<TransferRecoveryInspection> InspectTransferRecoveryUseCase::execute(const std::string& attemptId) {
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return Result<TransferRecoveryInspection>::failure(loaded.error());
    TransferRecoveryInspection report;
    report.attempt = loaded.value();
    const auto plan = history_.loadPlan(report.attempt.planId);
    if (!plan) return Result<TransferRecoveryInspection>::failure(plan.error());
    if (!trusted(plan.value(), report.attempt, hasher_)) {
        report.reason = "Plan signature or transfer paths/evidence are untrusted; no files inspected.";
        return Result<TransferRecoveryInspection>::success(std::move(report));
    }
    const auto step = std::find_if(plan.value().steps.begin(), plan.value().steps.end(), [&](const MoveStep& item) {
        return item.id == report.attempt.stepId;
    });
    const bool withZone = step->movement && step->movement->preservation == PreservationPolicy::LocalNtfsBasicWithZone;
    const auto source = withZone ? files_.inspectWithZone(report.attempt.source) : files_.inspect(report.attempt.source);
    const auto staging = withZone ? files_.inspectWithZone(report.attempt.staging) : files_.inspect(report.attempt.staging);
    const auto destination = withZone ? files_.inspectWithZone(report.attempt.destination) : files_.inspect(report.attempt.destination);
    for (const auto* observed : {&source, &staging, &destination})
        if (observed->error) report.observationErrors.push_back(*observed->error);
    report.source = classify(source, report.attempt.sourceFingerprint);
    report.staging = classify(staging, report.attempt.stagingFingerprint);
    // 확정 결과 기록이 없어도 검증된 임시 객체 ID로만 최종 파일을 대조한다.
    auto finalEvidence = report.attempt.destinationFingerprint;
    if (!finalEvidence && report.attempt.stagingFingerprint && report.attempt.stagingFingerprint->sha256)
        finalEvidence = report.attempt.stagingFingerprint;
    report.destination = classify(destination, finalEvidence);
    using State = TransferRecoveryFileState;
    using Finding = TransferRecoveryFinding;
    for (const auto state : {report.source, report.staging, report.destination}) {
        if (state == State::Unavailable || state == State::OtherVolume) {
            report.finding = Finding::Unavailable;
            report.reason = "A location is inaccessible or belongs to a different volume; absence cannot be assumed.";
            return Result<TransferRecoveryInspection>::success(std::move(report));
        }
    }
    for (const auto state : {report.source, report.staging, report.destination}) {
        if (state == State::Changed || state == State::Unowned) {
            report.finding = Finding::ChangedFile;
            report.reason = "A file changed or its ownership is unproven; matching names or contents do not authorize cleanup.";
            return Result<TransferRecoveryInspection>::success(std::move(report));
        }
    }
    const auto phase = report.attempt.phase;
    const bool sourceMatches = report.source == State::Matches;
    const bool noStage = report.staging == State::Missing;
    const bool noFinal = report.destination == State::Missing;
    report.finding = Finding::Ambiguous;
    report.reason = "File locations and recorded phase do not prove a safe outcome; manual review required.";
    if (sourceMatches && noStage && noFinal && !report.attempt.stagingFingerprint &&
        !report.attempt.destinationFingerprint && (phase == TransferPhase::Prepared || phase == TransferPhase::CopyIntent ||
        phase == TransferPhase::Failed || phase == TransferPhase::Cancelled || phase == TransferPhase::NeedsRecovery)) {
        report.finding = Finding::NoCopyObserved;
        report.reason = "Source is intact and no copy is observed; no automatic retry or record changes performed.";
    } else if (sourceMatches && noFinal && report.staging == State::ObjectOnly) {
        report.finding = Finding::PartialCopy;
        report.reason = "Recorded staging object exists but its complete content was never verified; preserve it for review.";
    } else if (sourceMatches && noFinal && report.staging == State::Matches && !report.attempt.destinationFingerprint) {
        report.finding = Finding::VerifiedStaging;
        report.reason = "Source and recorded verified staging copy match; final commit is not performed by recovery inspection.";
    } else if (sourceMatches && noStage && report.destination == State::Matches) {
        if (report.attempt.destinationFingerprint) {
            report.finding = Finding::CommittedSourceRetained;
            report.reason = "Recorded final copy and source both remain; this is not a completed move and neither is deleted.";
        } else if (phase == TransferPhase::CommitIntent || phase == TransferPhase::NeedsRecovery ||
            report.attempt.stagingCommit == StagingCommit::IntentRecorded) {
            report.finding = Finding::CommitNotRecorded;
            report.reason = "Verified staging object is at the final path, but commit result is not recorded; preserve both files.";
        }
    } else if (report.source == State::Missing && noStage && report.destination == State::Matches &&
        report.attempt.destinationFingerprint && ((phase >= TransferPhase::SourceRemoveIntent && phase <= TransferPhase::Completed) ||
            report.attempt.recoveryRemovalIntent)) {
        report.finding = Finding::SourceAbsentAfterRemoveIntent;
        report.reason = "Source is absent after a recorded removal intent and final copy matches; inspection does not finalize the record.";
    }
    if (withZone) {
        if (report.source == State::Missing && phase != TransferPhase::Completed) {
            const auto& proof = report.attempt.removalProof;
            const bool eligible = proof && (report.attempt.sourceRemovalIntentRecorded || report.attempt.recoveryRemovalIntent) &&
                TransferValidation::validateRemovalProof(report.attempt, *proof, plan.value().planHash) &&
                TransferValidation::reconcileCompleted(report.attempt, true) && noStage && report.destination == State::Matches;
            const auto checked = eligible ? files_.checkRemovalProof(report.attempt.destination, *proof) :
                Result<void>::failure({"recovery.zone_evidence_required", "No trusted pre-removal marker proof or removal intent remains."});
            if (!checked) {
                report.finding = Finding::Unavailable;
                report.reason = "Persisted removal proof cannot confirm the final file; file and records retained.";
                report.observationErrors.push_back(checked.error());
            } else {
                report.finding = Finding::SourceAbsentAfterRemoveIntent;
                report.reason = "Final object, marker and attributes match durable pre-removal evidence; explicit completion recovery is available.";
            }
        } else if (sourceMatches && (!noStage || !noFinal)) {
            const auto markers = files_.checkDownloadMarkers(report.attempt.source,
                noStage ? report.attempt.destination : report.attempt.staging, report.finding == Finding::PartialCopy);
            if (!markers) {
                report.finding = markers.error().code == "transfer.zone_changed" ? Finding::ChangedFile : Finding::Unavailable;
                report.reason = "Download markers do not prove safe recovery; files retained.";
                report.observationErrors.push_back(markers.error());
            }
        }
        if (report.attempt.direction == TransferDirection::Rollback && phase != TransferPhase::Completed) {
            report.finding = Finding::Unavailable;
            report.reason = "Interrupted reverse marker transfers still require manual review.";
            report.observationErrors.push_back({"recovery.zone_reverse_unsupported", report.reason});
        }
    }
    return Result<TransferRecoveryInspection>::success(std::move(report));
}

} // namespace nookbit
