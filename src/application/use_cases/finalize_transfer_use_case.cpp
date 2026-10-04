#include <nookbit/application/finalize_transfer_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <algorithm>
#include <cwctype>

namespace nookbit {
namespace {
// Windows 경로의 대소문자·구분자를 정규화해 비교하는 함수.
bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    auto a = left.lexically_normal().wstring(), b = right.lexically_normal().wstring();
#ifdef _WIN32
    for (auto& ch : a) ch = static_cast<wchar_t>(std::towlower(ch));
    for (auto& ch : b) ch = static_cast<wchar_t>(std::towlower(ch));
#endif
    return a == b;
}
}
// 외부에서 소유한 기록·파일 제거·계획 검증 포트를 빌려 받는 함수.
FinalizeTransferUseCase::FinalizeTransferUseCase(ITransferJournal& journal, ITransferSourceRemoval& removal,
    const IHistoryRepository& history, const IPlanHasher& hasher)
    : journal_(journal), removal_(removal), history_(history), hasher_(hasher) {}

// 제거 의도 저장 전 실패에는 제거하지 않고 이후 불확실성은 복구 필요로 남기는 함수.
Result<TransferAttempt> FinalizeTransferUseCase::execute(const std::string& attemptId,
    const std::function<bool()>& cancelled) {
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return Result<TransferAttempt>::failure(loaded.error());
    auto state = loaded.value();
    const bool forward = state.direction == TransferDirection::Forward;
    if (state.phase != TransferPhase::DestinationCommitted)
        return Result<TransferAttempt>::failure({"transfer.finalize_state", "Only a recorded committed copy may be finalized"});
    const auto plan = history_.loadPlan(state.planId);
    if (!plan) return Result<TransferAttempt>::failure(plan.error());
    const auto format = PlanSignature::validateFormat(plan.value());
    if (!format) return Result<TransferAttempt>::failure(format.error());
    const auto step = std::find_if(plan.value().steps.begin(), plan.value().steps.end(), [&](const MoveStep& item) {
        return item.id == state.stepId;
    });
    if (plan.value().status != (forward ? PlanStatus::Executing : PlanStatus::RollingBack) || step == plan.value().steps.end() ||
        step->status != (forward ? StepStatus::IntentRecorded : StepStatus::RollbackIntentRecorded) || !PlanSignature::isSha256Hex(plan.value().planHash) ||
        plan.value().planHash != hasher_.sha256(PlanSignature::approvalPayload(plan.value())) ||
        !samePath(forward ? step->source : step->destination, state.source) ||
        !samePath(forward ? step->destination : step->source, state.destination) ||
        !TransferValidation::sameContent(step->fingerprint, state.sourceFingerprint) ||
        (forward && (!TransferValidation::sameObject(step->fingerprint, state.sourceFingerprint) ||
        step->fingerprint.modifiedUtcMs != state.sourceFingerprint.modifiedUtcMs)))
        return Result<TransferAttempt>::failure({"transfer.finalize_untrusted", "Plan, source evidence or execution state is untrusted"});
    if (!forward) {
        const auto attempts = journal_.forStep(state.stepId);
        if (!attempts) return Result<TransferAttempt>::failure(attempts.error());
        const bool origin = std::any_of(attempts.value().begin(), attempts.value().end(), [&](const TransferAttempt& item) {
            return item.direction == TransferDirection::Forward && item.phase == TransferPhase::Completed &&
                item.destinationFingerprint && TransferValidation::sameObject(*item.destinationFingerprint, state.sourceFingerprint) &&
                TransferValidation::sameContent(*item.destinationFingerprint, state.sourceFingerprint) &&
                item.destinationFingerprint->modifiedUtcMs == state.sourceFingerprint.modifiedUtcMs;
        });
        if (!origin) return Result<TransferAttempt>::failure({"transfer.rollback_origin", "Completed forward result does not match rollback source"});
    }
    // 순수 계약과 저장이 성공했을 때만 로컬 단계를 갱신하는 함수.
    const auto record = [&](TransferPhase next, std::optional<Error> error = {}) -> Result<void> {
        TransferUpdate update{state.phase, next, {}, {}, error};
        const auto advanced = TransferValidation::advance(state, update);
        if (!advanced) return Result<void>::failure(advanced.error());
        const auto saved = journal_.advance(state.id, update);
        if (!saved) return saved;
        state = advanced.value();
        return Result<void>::success();
    };
    // 이미 제거되었을 수도 있는 결과를 단순 실패나 성공으로 숨기지 않는 함수.
    const auto stop = [&](Error error) -> Result<TransferAttempt> {
        const auto saved = record(TransferPhase::NeedsRecovery, error);
        if (!saved) {
            error.message += "; recovery record failed: " + saved.error().code;
            error.code = "transfer.record_failed";
        }
        return Result<TransferAttempt>::failure(std::move(error));
    };
    if (cancelled && cancelled()) return Result<TransferAttempt>::failure({"transfer.cancelled", "Cancelled; committed copy and source retained"});
    std::optional<MoveContract> movement;
    if (plan.value().formatVersion == 2) {
        movement = *step->movement;
        if (movement->strategy != MoveStrategy::VerifiedCopy)
            return stop({"transfer.strategy_invalid", "Removal requires an approved verified-copy strategy."});
        if (!forward) std::swap(movement->sourceVolume, movement->destinationVolume);
    }
    auto session = movement ? removal_.openApproved(state, *movement) : removal_.open(state);
    if (!session) return stop(session.error());
    const auto verified = session.value()->verify();
    if (!verified) return stop(verified.error());
    if (cancelled && cancelled()) return Result<TransferAttempt>::failure({"transfer.cancelled", "Cancelled before removal intent; both files retained"});
    const auto intent = record(TransferPhase::SourceRemoveIntent);
    if (!intent) return stop(intent.error());
    // 제거 의도 이후 취소 요청은 결과 판정·기록을 중단시키지 않는다.
    const auto removed = session.value()->removeVerifiedSource();
    if (!removed) return stop(removed.error());
    const auto saved = record(TransferPhase::SourceRemoved);
    if (!saved) return stop(saved.error());
    const auto completed = forward ? journal_.completeForward(state.id) : journal_.completeRollback(state.id);
    if (!completed) return stop(completed.error());
    state.phase = TransferPhase::Completed;
    return Result<TransferAttempt>::success(std::move(state));
}
} // namespace nookbit
