#include "transfer_recovery_binding.hpp"
#include "plan_signature.hpp"
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
// 복구 작업별 파일 정책과 분리해 승인 계획과 시도의 연결만 검증하는 함수.
Result<TransferRecoveryBinding> verifyTransferRecoveryBinding(const IHistoryRepository& history,
    const ITransferJournal& journal, const IPlanHasher& hasher, const TransferAttempt& attempt) {
    using Return = Result<TransferRecoveryBinding>;
    const auto saved = history.loadPlan(attempt.planId);
    if (!saved) return Return::failure(saved.error());
    const auto& plan = saved.value();
    const bool forward = attempt.direction == TransferDirection::Forward;
    // 해당 시도에 연결된 단계를 찾는 함수.
    const auto step = std::find_if(plan.steps.begin(), plan.steps.end(), [&](const MoveStep& item) {
        return item.id == attempt.stepId;
    });
    if (plan.id != attempt.planId || !PlanSignature::validateFormat(plan) || !PlanSignature::isSha256Hex(plan.planHash) ||
        plan.planHash != hasher.sha256(PlanSignature::approvalPayload(plan)) || step == plan.steps.end() ||
        (plan.status != PlanStatus::NeedsRecovery && plan.status != (forward ? PlanStatus::Executing : PlanStatus::RollingBack)) ||
        (step->status != StepStatus::NeedsRecovery && step->status != (forward ? StepStatus::IntentRecorded : StepStatus::RollbackIntentRecorded)) ||
        !samePath(forward ? step->source : step->destination, attempt.source) ||
        !samePath(forward ? step->destination : step->source, attempt.destination) ||
        !TransferValidation::sameContent(step->fingerprint, attempt.sourceFingerprint) ||
        (forward && (!TransferValidation::sameObject(step->fingerprint, attempt.sourceFingerprint) ||
        step->fingerprint.modifiedUtcMs != attempt.sourceFingerprint.modifiedUtcMs)))
        return Return::failure({"transfer.recovery_untrusted", "Signed plan, paths or recovery state do not match"});
    if (!forward) {
        const auto origins = journal.forStep(attempt.stepId);
        if (!origins) return Return::failure(origins.error());
        // 완료된 정방향 결과 하나와 역전송 원본의 객체·내용을 대조하는 함수.
        const auto count = std::count_if(origins.value().begin(), origins.value().end(), [&](const TransferAttempt& original) {
            return original.planId == attempt.planId && original.direction == TransferDirection::Forward &&
                original.phase == TransferPhase::Completed && original.destinationFingerprint &&
                TransferValidation::sameObject(*original.destinationFingerprint, attempt.sourceFingerprint) &&
                TransferValidation::sameContent(*original.destinationFingerprint, attempt.sourceFingerprint) &&
                original.destinationFingerprint->modifiedUtcMs == attempt.sourceFingerprint.modifiedUtcMs;
        });
        if (count != 1) return Return::failure({"transfer.rollback_origin", "One completed forward result is required"});
    }
    TransferRecoveryBinding binding{plan.planHash, {}};
    if (plan.formatVersion == 2) {
        binding.movement = *step->movement;
        if (binding.movement->strategy != MoveStrategy::VerifiedCopy)
            return Return::failure({"transfer.strategy_invalid", "Copy recovery cannot resolve a rename step"});
        if (!forward) std::swap(binding.movement->sourceVolume, binding.movement->destinationVolume);
    }
    return Return::success(std::move(binding));
}
} // namespace nookbit
