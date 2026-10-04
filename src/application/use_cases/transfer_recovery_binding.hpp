#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>

namespace nookbit {
struct TransferRecoveryBinding {
    std::string planHash;
    std::optional<MoveContract> movement;
};
/// 서명·경로·실행 상태·역전송 원본을 검사하고 승인 계약만 반환하는 함수.
[[nodiscard]] Result<TransferRecoveryBinding> verifyTransferRecoveryBinding(const IHistoryRepository& history,
    const ITransferJournal& journal, const IPlanHasher& hasher, const TransferAttempt& attempt);
} // namespace nookbit
