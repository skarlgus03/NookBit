#include <nookbit/application/execute_transfer_step_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <algorithm>

namespace nookbit {
// 승인 단계의 실행을 조율할 외부 포트를 빌려 받는 생성자.
ExecuteTransferStepUseCase::ExecuteTransferStepUseCase(IStagedCopy& copy, ITransferSourceRemoval& removal,
    ITransferJournal& journal, const IHistoryRepository& history, const IPlanHasher& hasher)
    : copy_(copy), removal_(removal), journal_(journal), history_(history), hasher_(hasher) {}

// 저장된 승인 계약만 사용하며 결과 불확실성에 원본 또는 복사본을 자동 정리하지 않는 함수.
Result<void> ExecuteTransferStepUseCase::execute(const PlanId& planId, const StepId& stepId,
    const std::string& attemptId, const std::filesystem::path& staging) {
    const auto loaded = history_.loadPlan(planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    const auto& plan = loaded.value();
    const auto format = PlanSignature::validateFormat(plan);
    if (!format) return format;
    if (plan.formatVersion != 2 || plan.status != PlanStatus::Executing ||
        !PlanSignature::isSha256Hex(plan.planHash) || plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan)))
        return Result<void>::failure({"transfer.approval_invalid", "An intact executing v2 approval is required."});
    const auto step = std::find_if(plan.steps.begin(), plan.steps.end(), [&](const MoveStep& item) { return item.id == stepId; });
    if (step == plan.steps.end() || step->status != StepStatus::IntentRecorded ||
        step->movement->strategy != MoveStrategy::VerifiedCopy)
        return Result<void>::failure({"transfer.step_invalid", "A recorded verified-copy intent is required."});
    TransferAttempt attempt;
    attempt.id = attemptId;
    attempt.planId = planId;
    attempt.stepId = stepId;
    attempt.source = step->source;
    attempt.destination = step->destination;
    attempt.staging = staging;
    attempt.sourceFingerprint = step->fingerprint;
    CommitTransferCopyUseCase copy(copy_, journal_);
    const auto committed = copy.execute(attempt, {}, &*step->movement);
    if (!committed) return Result<void>::failure(committed.error());
    FinalizeTransferUseCase finalize(journal_, removal_, history_, hasher_);
    const auto completed = finalize.execute(attemptId);
    if (!completed) return Result<void>::failure(completed.error());
    return Result<void>::success();
}
}
