#include <nookbit/application/reconcile_transfer_recovery_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <algorithm>
#include <cwctype>

namespace nookbit {
namespace {
// Windows 경로의 대소문자 차이를 정규화해 비교하는 함수.
bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    auto a = left.lexically_normal().wstring(), b = right.lexically_normal().wstring();
#ifdef _WIN32
    for (auto& ch : a) ch = static_cast<wchar_t>(std::towlower(ch));
    for (auto& ch : b) ch = static_cast<wchar_t>(std::towlower(ch));
#endif
    return a == b;
}
}

// 외부에서 소유하는 복구 기록·보호 검사 포트를 빌려 받는 함수.
ReconcileTransferRecoveryUseCase::ReconcileTransferRecoveryUseCase(const IHistoryRepository& history,
    ITransferJournal& journal, ITransferRecoveryGuard& guard, const IPlanHasher& hasher)
    : history_(history), journal_(journal), guard_(guard), hasher_(hasher) {}

// 파일 삭제 없이 검증된 완료 사실과 이력만 복구하는 함수.
Result<TransferAttempt> ReconcileTransferRecoveryUseCase::execute(const std::string& attemptId) {
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return Result<TransferAttempt>::failure(loaded.error());
    const auto& attempt = loaded.value();
    // 실제 제거 의도 존재 여부는 이벤트를 재생하는 저널이 트랜잭션 안에서 확인한다.
    const auto next = TransferValidation::reconcileCompleted(attempt, true);
    if (!next) return next;
    const auto saved = history_.loadPlan(attempt.planId);
    if (!saved) return Result<TransferAttempt>::failure(saved.error());
    const auto& plan = saved.value();
    const bool forward = attempt.direction == TransferDirection::Forward;
    const auto step = std::find_if(plan.steps.begin(), plan.steps.end(), [&](const MoveStep& item) {
        return item.id == attempt.stepId;
    });
    if (!PlanSignature::validateFormat(plan) || !PlanSignature::isSha256Hex(plan.planHash) ||
        plan.planHash != hasher_.sha256(PlanSignature::approvalPayload(plan)) || step == plan.steps.end() ||
        (plan.status != PlanStatus::NeedsRecovery && plan.status != (forward ? PlanStatus::Executing : PlanStatus::RollingBack)) ||
        (step->status != StepStatus::NeedsRecovery && step->status != (forward ? StepStatus::IntentRecorded : StepStatus::RollbackIntentRecorded)) ||
        !samePath(forward ? step->source : step->destination, attempt.source) ||
        !samePath(forward ? step->destination : step->source, attempt.destination) ||
        !TransferValidation::sameContent(step->fingerprint, attempt.sourceFingerprint) ||
        (forward && (!TransferValidation::sameObject(step->fingerprint, attempt.sourceFingerprint) ||
        step->fingerprint.modifiedUtcMs != attempt.sourceFingerprint.modifiedUtcMs)))
        return Result<TransferAttempt>::failure({"transfer.recovery_untrusted", "Signed plan, paths or recovery state do not match"});
    if (!forward) {
        const auto origins = journal_.forStep(attempt.stepId);
        if (!origins) return Result<TransferAttempt>::failure(origins.error());
        const auto count = std::count_if(origins.value().begin(), origins.value().end(), [&](const TransferAttempt& original) {
            return original.planId == attempt.planId && original.direction == TransferDirection::Forward &&
                original.phase == TransferPhase::Completed && original.destinationFingerprint &&
                TransferValidation::sameObject(*original.destinationFingerprint, attempt.sourceFingerprint) &&
                TransferValidation::sameContent(*original.destinationFingerprint, attempt.sourceFingerprint) &&
                original.destinationFingerprint->modifiedUtcMs == attempt.sourceFingerprint.modifiedUtcMs;
        });
        if (count != 1) return Result<TransferAttempt>::failure({"transfer.rollback_origin", "One completed forward result is required"});
    }
    std::optional<MoveContract> movement;
    if (plan.formatVersion == 2) {
        movement = *step->movement;
        if (movement->strategy != MoveStrategy::VerifiedCopy)
            return Result<TransferAttempt>::failure({"transfer.strategy_invalid", "Copy recovery cannot resolve a rename step"});
        if (!forward) std::swap(movement->sourceVolume, movement->destinationVolume);
    }
    auto session = guard_.open(attempt, movement ? &*movement : nullptr);
    if (!session) return Result<TransferAttempt>::failure(session.error());
    const auto verified = session.value()->verify();
    if (!verified) return Result<TransferAttempt>::failure(verified.error());
    const auto completed = journal_.reconcileCompleted(attempt, plan.planHash);
    if (!completed) return Result<TransferAttempt>::failure(completed.error());
    return next;
}
} // namespace nookbit
