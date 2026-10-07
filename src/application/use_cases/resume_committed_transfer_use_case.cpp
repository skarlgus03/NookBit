#include <nookbit/application/resume_committed_transfer_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {
// 외부에서 생성한 기록·검증 일꾼들을 빌려 받는 함수.
ResumeCommittedTransferUseCase::ResumeCommittedTransferUseCase(const IHistoryRepository& history, ITransferJournal& journal,
    ITransferSourceRemoval& removal, ITransferRecoveryGuard& guard, const IPlanHasher& hasher)
    : history_(history), journal_(journal), removal_(removal), guard_(guard), hasher_(hasher) {}

// 보호된 확정 전송에 별도 제거 의도를 저장하고 완료까지 마무리하는 함수.
Result<TransferAttempt> ResumeCommittedTransferUseCase::execute(const std::string& attemptId,
    const std::function<bool()>& cancelled) {
    using Return = Result<TransferAttempt>;
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return Return::failure(loaded.error());
    auto attempt = loaded.value();
    auto eligible = attempt;
    eligible.recoveryRemovalIntent = false;
    const auto intent = TransferValidation::recordRecoveryRemovalIntent(eligible);
    if (!intent) return intent;
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, attempt);
    if (!binding) return Return::failure(binding.error());
    const auto& movement = binding.value().movement;
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Both copies retained before recovery removal"});
    // 제거 뒤 DB 저장에 실패한 재시도는 파일을 다시 삭제하지 않고 완료 사실만 검증한다.
    if (attempt.recoveryRemovalIntent) {
        auto finished = guard_.open(attempt, movement ? &*movement : nullptr);
        if (finished && finished.value()->verify()) {
            const auto saved = journal_.reconcileCompleted(attempt, binding.value().planHash);
            if (!saved) return Return::failure(saved.error());
            return TransferValidation::reconcileCompleted(attempt, true);
        }
    }
    auto session = movement ? removal_.openApproved(attempt, *movement) : removal_.open(attempt);
    if (!session) return Return::failure(session.error());
    const auto verified = session.value()->verify();
    if (!verified) return Return::failure(verified.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Both copies retained before recovery intent"});
    if (attempt.direction == TransferDirection::Forward && movement && movement->preservation == PreservationPolicy::LocalNtfsBasicWithZone) {
        const auto proof = session.value()->removalProof(binding.value().planHash);
        if (!proof) return Return::failure(proof.error());
        const auto savedProof = journal_.recordRemovalProof(attempt, proof.value());
        if (!savedProof) return Return::failure(savedProof.error());
        attempt.removalProof = proof.value();
    }
    // 이전 의도가 있어도 현재 승인·예상 상태를 보호 세션 안에서 다시 확인한다.
    const auto savedIntent = journal_.recordRecoveryRemovalIntent(attempt, binding.value().planHash);
    if (!savedIntent) return Return::failure(savedIntent.error());
    if (!attempt.recoveryRemovalIntent) attempt = TransferValidation::recordRecoveryRemovalIntent(attempt).value();
    // 의도 이후에는 취소로 결과 판정을 생략하지 않으며 같은 보호 원본만 제거한다.
    const auto removed = session.value()->removeVerifiedSource();
    if (!removed) return Return::failure(removed.error());
    std::unique_ptr<ITransferRecoverySession> final;
    if (movement && movement->preservation == PreservationPolicy::LocalNtfsBasicWithZone) {
        // 원본 없는 새 세션은 표식의 과거 증거가 없으므로 같은 제거 세션을 유지한다.
        const auto checked = session.value()->verifyRemovedSource();
        if (!checked) return Return::failure(checked.error());
    } else {
        session.value().reset();
        auto opened = guard_.open(attempt, movement ? &*movement : nullptr);
        if (!opened) return Return::failure(opened.error());
        final = std::move(opened.value());
        const auto checked = final->verify();
        if (!checked) return Return::failure(checked.error());
    }
    const auto saved = journal_.reconcileCompleted(attempt, binding.value().planHash);
    if (!saved) return Return::failure(saved.error());
    return TransferValidation::reconcileCompleted(attempt, true);
}
}
