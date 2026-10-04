#include <nookbit/application/reconcile_transfer_commit_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {
// 생성된 일꾼의 소유권을 가져오지 않고 참조를 받는 함수.
ReconcileTransferCommitUseCase::ReconcileTransferCommitUseCase(const IHistoryRepository& history,
    ITransferJournal& journal, ITransferCommitRecoveryGuard& guard, const IPlanHasher& hasher)
    : history_(history), journal_(journal), guard_(guard), hasher_(hasher) {}

// 보호된 두 복사본을 유지한 채 최종 확정 결과만 복구하는 함수.
Result<TransferAttempt> ReconcileTransferCommitUseCase::execute(const std::string& attemptId,
    const std::function<bool()>& cancelled) {
    using Return = Result<TransferAttempt>;
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return loaded;
    const auto& attempt = loaded.value();
    if (!attempt.stagingFingerprint) return Return::failure({"transfer.commit_recovery_state", "Verified staging evidence required"});
    const auto eligible = TransferValidation::reconcileDestinationCommitted(attempt, *attempt.stagingFingerprint, true);
    if (!eligible) return eligible;
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, attempt);
    if (!binding) return Return::failure(binding.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Commit recovery cancelled before record changes"});
    const auto& movement = binding.value().movement;
    auto session = guard_.open(attempt, movement ? &*movement : nullptr);
    if (!session) return Return::failure(session.error());
    const auto verified = session.value()->verify(cancelled);
    if (!verified) return Return::failure(verified.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Commit recovery cancelled before record changes"});
    const auto saved = journal_.reconcileDestinationCommitted(attempt, binding.value().planHash, verified.value());
    if (!saved) return Return::failure(saved.error());
    return TransferValidation::reconcileDestinationCommitted(attempt, verified.value(), true);
}
}
