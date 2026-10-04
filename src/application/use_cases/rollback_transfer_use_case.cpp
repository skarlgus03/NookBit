#include <nookbit/application/rollback_transfer_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include <algorithm>

namespace nookbit {
// 외부에서 소유한 전송·검증·기록 포트를 빌려 받는 함수.
RollbackTransferUseCase::RollbackTransferUseCase(ITransferJournal& journal, IHistoryRepository& history,
    IStagedCopy& copy, ITransferSourceRemoval& removal, ITransferFileInspector& files, const IPlanHasher& hasher)
    : journal_(journal), history_(history), copy_(copy), removal_(removal), files_(files), hasher_(hasher) {}

// 충돌·내용 변경을 먼저 확인하고 정방향과 별개인 역전송 시도를 수행하는 함수.
Result<TransferAttempt> RollbackTransferUseCase::execute(const std::string& forwardAttemptId,
    const std::string& rollbackAttemptId, const std::filesystem::path& staging, const std::function<bool()>& cancelled) {
    return executeImpl(forwardAttemptId, rollbackAttemptId, staging, cancelled, false);
}

// 읽기 전용 점검과 실제 역전송에서 같은 기록·파일 검증을 사용하는 함수.
Result<TransferAttempt> RollbackTransferUseCase::executeImpl(const std::string& forwardAttemptId,
    const std::string& rollbackAttemptId, const std::filesystem::path& staging, const std::function<bool()>& cancelled, bool inspectOnly) {
    if (cancelled && cancelled()) return Result<TransferAttempt>::failure({"transfer.cancelled", "Rollback cancelled before any changes"});
    const auto forward = journal_.load(forwardAttemptId);
    if (!forward) return Result<TransferAttempt>::failure(forward.error());
    if (forward.value().direction != TransferDirection::Forward || forward.value().phase != TransferPhase::Completed ||
        !forward.value().destinationFingerprint)
        return Result<TransferAttempt>::failure({"transfer.rollback_origin", "A completed forward result is required"});
    const auto plan = history_.loadPlan(forward.value().planId);
    if (!plan) return Result<TransferAttempt>::failure(plan.error());
    const auto format = PlanSignature::validateFormat(plan.value());
    if (!format) return Result<TransferAttempt>::failure(format.error());
    const auto step = std::find_if(plan.value().steps.begin(), plan.value().steps.end(), [&](const MoveStep& item) {
        return item.id == forward.value().stepId;
    });
    if (!PlanSignature::isSha256Hex(plan.value().planHash) ||
        plan.value().planHash != hasher_.sha256(PlanSignature::approvalPayload(plan.value())) ||
        step == plan.value().steps.end() ||
        (plan.value().status != PlanStatus::Completed && plan.value().status != PlanStatus::RollingBack &&
            !(inspectOnly && (plan.value().status == PlanStatus::Executing || plan.value().status == PlanStatus::Failed))) ||
        (step->status != StepStatus::Applied && step->status != StepStatus::RollbackIntentRecorded) ||
        !TransferValidation::sameObject(step->fingerprint, forward.value().sourceFingerprint) ||
        !TransferValidation::sameContent(step->fingerprint, *forward.value().destinationFingerprint))
        return Result<TransferAttempt>::failure({"transfer.rollback_untrusted", "Plan signature or original transfer is untrusted"});
    std::optional<MoveContract> reverseMovement;
    if (plan.value().formatVersion == 2) {
        if (step->movement->strategy != MoveStrategy::VerifiedCopy)
            return Result<TransferAttempt>::failure({"transfer.strategy_invalid", "Reverse copy requires a verified-copy approval."});
        PrepareTransferPlanUseCase prepare(copy_);
        const auto current = prepare.verify(plan.value());
        if (!current) return Result<TransferAttempt>::failure(current.error());
        reverseMovement = *step->movement;
        std::swap(reverseMovement->sourceVolume, reverseMovement->destinationVolume);
    }
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
        if ((!inspectOnly && prior.id == rollbackAttemptId) || (!TransferValidation::isTerminal(prior.phase)) ||
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
    if (inspectOnly) return Result<TransferAttempt>::success(std::move(undo));
    if (plan.value().status == PlanStatus::Completed) {
        const auto rolling = history_.appendEvent(PlanStatusChanged{undo.planId, PlanStatus::RollingBack});
        if (!rolling) return Result<TransferAttempt>::failure(rolling.error());
    }
    if (step->status == StepStatus::Applied) {
        const auto intent = history_.appendEvent(StepStatusChanged{undo.planId, undo.stepId, StepStatus::RollbackIntentRecorded});
        if (!intent) return Result<TransferAttempt>::failure(intent.error());
    }
    CommitTransferCopyUseCase copy(copy_, journal_);
    const auto committed = copy.execute(undo, cancelled, reverseMovement ? &*reverseMovement : nullptr);
    if (!committed) return committed;
    FinalizeTransferUseCase finalize(journal_, removal_, history_, hasher_);
    return finalize.execute(undo.id, cancelled);
}

// 이전 성공 결과를 모호하게 선택하거나 다른 계획의 기록을 재사용하지 않는 함수.
Result<std::string> RollbackTransferUseCase::findForward(const PlanId& planId, const StepId& stepId) {
    const auto attempts = journal_.forStep(stepId);
    if (!attempts) return Result<std::string>::failure(attempts.error());
    const TransferAttempt* origin = nullptr;
    for (const auto& attempt : attempts.value()) {
        if (attempt.planId == planId && attempt.direction == TransferDirection::Forward && attempt.phase == TransferPhase::Completed) {
            if (origin) return Result<std::string>::failure({"transfer.rollback_ambiguous", "Multiple forward results require review."});
            origin = &attempt;
        }
    }
    if (!origin) return Result<std::string>::failure({"transfer.rollback_origin", "No completed forward result for this plan and step."});
    return Result<std::string>::success(origin->id);
}

// 배치 되돌리기를 시작하기 전에 해당 단계의 안전 조건을 읽기 전용으로 검사하는 함수.
Result<void> RollbackTransferUseCase::verifyRollback(const PlanId& planId, const StepId& stepId) {
    const auto origin = findForward(planId, stepId);
    if (!origin) return Result<void>::failure(origin.error());
    const auto forward = journal_.load(origin.value());
    if (!forward) return Result<void>::failure(forward.error());
    const auto result = executeImpl(origin.value(), "preflight", forward.value().source.parent_path() / ".nookbit-preflight.tmp", {}, true);
    if (!result) return Result<void>::failure(result.error());
    return Result<void>::success();
}

// 사전 점검 후에도 실제 실행 직전에 동일한 조건을 다시 검사하는 함수.
Result<void> RollbackTransferUseCase::rollback(const PlanId& planId, const StepId& stepId,
    const std::string& attemptId, const std::filesystem::path& staging) {
    const auto origin = findForward(planId, stepId);
    if (!origin) return Result<void>::failure(origin.error());
    const auto result = execute(origin.value(), attemptId, staging);
    if (!result) return Result<void>::failure(result.error());
    return Result<void>::success();
}
} // namespace nookbit
