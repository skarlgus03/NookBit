#include <nookbit/application/rollback_transfer_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <algorithm>

namespace nookbit {
// 외부에서 소유한 전송·검증·기록 포트를 빌려 받는 함수.
RollbackTransferUseCase::RollbackTransferUseCase(ITransferJournal& journal, IHistoryRepository& history,
    IStagedCopy& copy, ITransferSourceRemoval& removal, ITransferFileInspector& files, const IPlanHasher& hasher)
    : journal_(journal), history_(history), copy_(copy), removal_(removal), files_(files), hasher_(hasher) {}

// 충돌·내용 변경을 먼저 확인하고 정방향과 별개인 역전송 시도를 수행하는 함수.
Result<TransferAttempt> RollbackTransferUseCase::execute(const std::string& forwardAttemptId,
    const std::string& rollbackAttemptId, const std::filesystem::path& staging, const std::function<bool()>& cancelled) {
    if (cancelled && cancelled()) return Result<TransferAttempt>::failure({"transfer.cancelled", "Rollback cancelled before any changes"});
    const auto forward = journal_.load(forwardAttemptId);
    if (!forward) return Result<TransferAttempt>::failure(forward.error());
    if (forward.value().direction != TransferDirection::Forward || forward.value().phase != TransferPhase::Completed ||
        !forward.value().destinationFingerprint)
        return Result<TransferAttempt>::failure({"transfer.rollback_origin", "A completed forward result is required"});
    const auto plan = history_.loadPlan(forward.value().planId);
    if (!plan) return Result<TransferAttempt>::failure(plan.error());
    if (!PlanSignature::validateFormat(plan.value()) || plan.value().formatVersion != 1)
        return Result<TransferAttempt>::failure({"transfer.volume_verifier_required", "V2 rollback requires current volume verification."});
    const auto step = std::find_if(plan.value().steps.begin(), plan.value().steps.end(), [&](const MoveStep& item) {
        return item.id == forward.value().stepId;
    });
    if (!PlanSignature::isSha256Hex(plan.value().planHash) ||
        plan.value().planHash != hasher_.sha256(PlanSignature::approvalPayload(plan.value())) ||
        step == plan.value().steps.end() ||
        (plan.value().status != PlanStatus::Completed && plan.value().status != PlanStatus::RollingBack) ||
        (step->status != StepStatus::Applied && step->status != StepStatus::RollbackIntentRecorded) ||
        !TransferValidation::sameObject(step->fingerprint, forward.value().sourceFingerprint) ||
        !TransferValidation::sameContent(step->fingerprint, *forward.value().destinationFingerprint))
        return Result<TransferAttempt>::failure({"transfer.rollback_untrusted", "Plan signature or original transfer is untrusted"});
    TransferAttempt undo;
    undo.id = rollbackAttemptId;
    undo.planId = plan.value().id;
    undo.stepId = step->id;
    undo.direction = TransferDirection::Rollback;
    undo.source = step->destination;
    undo.destination = step->source;
    undo.staging = staging;
    undo.sourceFingerprint = *forward.value().destinationFingerprint;
    const auto valid = TransferValidation::validateNew(undo);
    if (!valid) return Result<TransferAttempt>::failure(valid.error());
    const auto attempts = journal_.forStep(step->id);
    if (!attempts) return Result<TransferAttempt>::failure(attempts.error());
    for (const auto& prior : attempts.value()) {
        if (prior.id == rollbackAttemptId || (!TransferValidation::isTerminal(prior.phase)) ||
            prior.phase == TransferPhase::NeedsRecovery ||
            (prior.direction == TransferDirection::Rollback && prior.phase == TransferPhase::Completed))
            return Result<TransferAttempt>::failure({"transfer.attempt_conflict", "Rollback is unresolved, already completed or the ID is reused"});
    }
    const auto source = files_.inspect(undo.source);
    const auto target = files_.inspect(undo.destination);
    if (source.presence != TransferPathPresence::Present || !source.fingerprint ||
        !TransferValidation::sameObject(undo.sourceFingerprint, *source.fingerprint) ||
        !TransferValidation::sameContent(undo.sourceFingerprint, *source.fingerprint) ||
        undo.sourceFingerprint.modifiedUtcMs != source.fingerprint->modifiedUtcMs)
        return Result<TransferAttempt>::failure({"transfer.rollback_source_changed", "Moved file is unavailable or no longer matches the recorded result"});
    if (target.presence != TransferPathPresence::Missing || target.volumeId != step->fingerprint.volumeId)
        return Result<TransferAttempt>::failure({"transfer.rollback_destination", "Original location must be absent on its recorded volume; no rename or overwrite fallback"});
    if (cancelled && cancelled()) return Result<TransferAttempt>::failure({"transfer.cancelled", "Rollback cancelled before intent"});
    if (plan.value().status == PlanStatus::Completed) {
        const auto rolling = history_.appendEvent(PlanStatusChanged{undo.planId, PlanStatus::RollingBack});
        if (!rolling) return Result<TransferAttempt>::failure(rolling.error());
    }
    if (step->status == StepStatus::Applied) {
        const auto intent = history_.appendEvent(StepStatusChanged{undo.planId, undo.stepId, StepStatus::RollbackIntentRecorded});
        if (!intent) return Result<TransferAttempt>::failure(intent.error());
    }
    CommitTransferCopyUseCase copy(copy_, journal_);
    const auto committed = copy.execute(undo, cancelled);
    if (!committed) return committed;
    FinalizeTransferUseCase finalize(journal_, removal_, history_, hasher_);
    return finalize.execute(undo.id, cancelled);
}
} // namespace nookbit
