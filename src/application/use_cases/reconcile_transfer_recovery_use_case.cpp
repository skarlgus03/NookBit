#include <nookbit/application/reconcile_transfer_recovery_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {

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
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, attempt);
    if (!binding) return Result<TransferAttempt>::failure(binding.error());
    const auto& movement = binding.value().movement;
    auto session = guard_.open(attempt, movement ? &*movement : nullptr);
    if (!session) return Result<TransferAttempt>::failure(session.error());
    const auto verified = session.value()->verify();
    if (!verified) return Result<TransferAttempt>::failure(verified.error());
    const auto completed = journal_.reconcileCompleted(attempt, binding.value().planHash);
    if (!completed) return Result<TransferAttempt>::failure(completed.error());
    return next;
}
} // namespace nookbit
