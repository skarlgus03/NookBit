#pragma once
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/reconcile_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 중단된 확정 전송만 명시적 제거 의도·보호 제거·완료 기록으로 마무리하는 조율자.
class ResumeCommittedTransferUseCase final {
public:
    /// 외부 소유의 기록·원본 제거·완료 보호·서명 검증 포트를 빌려 받는 함수.
    ResumeCommittedTransferUseCase(const IHistoryRepository& history, ITransferJournal& journal,
        ITransferSourceRemoval& removal, ITransferRecoveryGuard& guard, const IPlanHasher& hasher);
    /// 기록된 최종 복사본과 원본만 검증해 마무리하며 부분 복사는 재개하지 않는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId,
        const std::function<bool()>& cancelled = {});
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    ITransferSourceRemoval& removal_;
    ITransferRecoveryGuard& guard_;
    const IPlanHasher& hasher_;
};
}
