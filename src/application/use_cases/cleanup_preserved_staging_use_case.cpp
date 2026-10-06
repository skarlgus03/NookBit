#include <nookbit/application/cleanup_preserved_staging_use_case.hpp>
#include "plan_signature.hpp"
#include <algorithm>

namespace nookbit {
namespace {
// 플랫폼 구현을 참조하지 않고 코어의 객체·내용·수정 시각 계약을 비교하는 함수.
bool sameApprovedFingerprint(const FileFingerprint& left, const FileFingerprint& right) {
    return TransferValidation::sameObject(left, right) && TransferValidation::sameContent(left, right) && left.modifiedUtcMs == right.modifiedUtcMs;
}
}
// 외부에서 조립한 조회·정리·감사 기능을 빌려 받는 함수.
CleanupPreservedStagingUseCase::CleanupPreservedStagingUseCase(IHistoryRepository& history, const ITransferJournal& journal,
    ITransferFileInspector& files, IPreservedStagingCleanup& cleanup, const IPlanHasher& hasher)
    : history_(history), journal_(journal), files_(files), cleanup_(cleanup), hasher_(hasher) {}

// 완료된 되돌리기의 승인 계획과 보존한 정방향 부분 시도만 연결하는 함수.
Result<MoveContract> CleanupPreservedStagingUseCase::bind(const PreservedCleanupRecord& request, const TransferAttempt& attempt) {
    using Return = Result<MoveContract>;
    const auto loaded = history_.loadPlan(request.planId);
    const auto entries = history_.preservedTransfers(request.planId);
    if (!loaded || !entries) return Return::failure(!loaded ? loaded.error() : entries.error());
    const auto& plan = loaded.value();
    const auto step = std::find_if(plan.steps.begin(), plan.steps.end(), [&](const MoveStep& item) { return item.id == attempt.stepId; });
    if (request.removed || plan.id != request.planId || plan.status != PlanStatus::RolledBack || plan.id != attempt.planId || request.attemptId != attempt.id ||
        request.planHash != plan.planHash || !PlanSignature::validateFormat(plan) || !PlanSignature::validateMovementScope(plan) ||
        !PlanSignature::isSha256Hex(plan.planHash) || plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan)) ||
        step == plan.steps.end() || step->status != StepStatus::Planned || !step->movement ||
        step->movement->strategy != MoveStrategy::VerifiedCopy || attempt.source != step->source || attempt.destination != step->destination ||
        !sameApprovedFingerprint(attempt.sourceFingerprint, step->fingerprint) || attempt.direction != TransferDirection::Forward ||
        !attempt.stagingFingerprint || attempt.destinationFingerprint || attempt.recoveryCompleted || attempt.recoveryRemovalIntent ||
        attempt.stagingCleanup != StagingCleanup::None || attempt.copyResume != CopyResume::None || attempt.stagingCommit != StagingCommit::None ||
        (attempt.phase != TransferPhase::StagingCreated && attempt.phase != TransferPhase::Cancelled &&
         attempt.phase != TransferPhase::Failed && attempt.phase != TransferPhase::NeedsRecovery) ||
        std::none_of(entries.value().begin(), entries.value().end(), [&](const PreservedTransfer& item) {
            return item.stepId == attempt.stepId && item.attemptId == attempt.id && item.staging == attempt.staging;
        })) return Return::failure({"cleanup.binding", "Signed completed undo and preserved partial attempt required."});
    const auto attempts = journal_.forStep(attempt.stepId);
    if (!attempts) return Return::failure(attempts.error());
    if (attempts.value().size() != 1 || attempts.value()[0].id != attempt.id)
        return Return::failure({"cleanup.ambiguous", "Multiple transfer attempts block partial cleanup."});
    return Return::success(*step->movement);
}

// 원본과 부분 임시 파일을 변경하지 않고 승인 대상의 새 지문을 읽는 함수.
Result<PreservedCleanupRecord> CleanupPreservedStagingUseCase::inspect(const PlanId& planId, const std::string& attemptId) {
    using Return = Result<PreservedCleanupRecord>;
    const auto plan = history_.loadPlan(planId);
    const auto loaded = journal_.load(attemptId);
    const auto prior = history_.preservedCleanup(planId, attemptId);
    if (!plan || !loaded || !prior) return Return::failure(!plan ? plan.error() : !loaded ? loaded.error() : prior.error());
    if (prior.value() && prior.value()->removed) return Return::failure({"cleanup.already_removed", "Cleanup is already complete."});
    PreservedCleanupRecord result{planId, plan.value().planHash, attemptId, {}, false};
    const auto binding = bind(result, loaded.value());
    if (!binding) return Return::failure(binding.error());
    const auto source = files_.inspect(loaded.value().source);
    const auto stage = files_.inspect(loaded.value().staging);
    const auto final = files_.inspect(loaded.value().destination);
    if (source.error || stage.error || final.error || source.presence != TransferPathPresence::Present || !source.fingerprint ||
        !sameApprovedFingerprint(loaded.value().sourceFingerprint, *source.fingerprint) || final.presence != TransferPathPresence::Missing)
        return Return::failure({"cleanup.locations", "Unchanged original and absent final path required."});
    if (stage.presence == TransferPathPresence::Missing && prior.value()) result = *prior.value();
    else if (stage.presence == TransferPathPresence::Present && stage.fingerprint && stage.fingerprint->sha256 &&
        PlanSignature::isSha256Hex(*stage.fingerprint->sha256) && TransferValidation::sameObject(*loaded.value().stagingFingerprint, *stage.fingerprint) &&
        stage.fingerprint->size < source.fingerprint->size) result.stagingFingerprint = *stage.fingerprint;
    else return Return::failure({"cleanup.staging", "Recorded partial staging object required; unknown absence is not cleanup proof."});
    if (prior.value() && (!sameApprovedFingerprint(prior.value()->stagingFingerprint, result.stagingFingerprint) || prior.value()->planHash != result.planHash))
        return Return::failure({"cleanup.intent_changed", "Staging differs from the recorded cleanup approval."});
    return Return::success(std::move(result));
}

// 조회 당시의 명시적 승인을 재검사하고 의도 감사 이후에만 임시 파일을 제거하는 함수.
Result<void> CleanupPreservedStagingUseCase::execute(const PreservedCleanupRequest& request) {
    if (!request.approved) return Result<void>::failure({"cleanup.approval_required", "Explicit staging cleanup approval required."});
    const auto observed = inspect(request.expected.planId, request.expected.attemptId);
    if (!observed) return Result<void>::failure(observed.error());
    if (request.expected.removed || request.expected.planHash != observed.value().planHash ||
        !sameApprovedFingerprint(request.expected.stagingFingerprint, observed.value().stagingFingerprint))
        return Result<void>::failure({"cleanup.stale_approval", "Cleanup preview changed; inspect and approve again."});
    const auto loaded = journal_.load(request.expected.attemptId);
    const auto prior = history_.preservedCleanup(request.expected.planId, request.expected.attemptId);
    if (!loaded || !prior) return Result<void>::failure(!loaded ? loaded.error() : prior.error());
    const auto movement = bind(request.expected, loaded.value());
    if (!movement) return Result<void>::failure(movement.error());
    auto session = cleanup_.openPreserved(loaded.value(), movement.value(), observed.value().stagingFingerprint, prior.value().has_value());
    if (!session) return Result<void>::failure(session.error());
    const auto verified = session.value()->verify();
    if (!verified) return verified;
    if (!prior.value()) {
        const auto saved = history_.recordPreservedCleanup(observed.value());
        if (!saved) return saved;
    }
    const auto removed = session.value()->removeVerifiedStaging();
    if (!removed) return removed;
    auto completion = observed.value(); completion.removed = true;
    return history_.recordPreservedCleanup(completion);
}
}
