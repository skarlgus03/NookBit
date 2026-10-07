#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>
#include <functional>
#include <memory>

namespace nookbit {
class ITransferRemovalSession {
public:
    /// 제거 예약 없이 핸들만 해제하는 함수.
    virtual ~ITransferRemovalSession() = default;
    /// 보호된 원본과 최종 복사본을 다시 검증하는 함수.
    [[nodiscard]] virtual Result<void> verify() = 0;
    /// 현재 보호·검증 중인 최종 파일과 표식의 영구 저장용 증거를 반환하는 함수.
    [[nodiscard]] virtual Result<TransferRemovalProof> removalProof(const std::string&) {
        return Result<TransferRemovalProof>::failure({"transfer.removal_proof_unsupported", "Protected removal proof is required."});
    }
    /// 같은 보호 세션의 검증된 원본만 한 번 제거하고 실제 부재를 확인하는 함수.
    [[nodiscard]] virtual Result<void> removeVerifiedSource() = 0;
    /// 제거 전부터 유지한 보호 증거로 원본 부재와 최종 복사본을 확인하는 함수.
    [[nodiscard]] virtual Result<void> verifyRemovedSource() {
        return Result<void>::failure({"transfer.removal_verification_unsupported", "Live removal evidence is required."});
    }
};
class ITransferSourceRemoval {
public:
    /// 제거 일꾼을 해제하는 함수.
    virtual ~ITransferSourceRemoval() = default;
    /// 확정 증거가 있는 원본·최종 파일을 보호하고 검증하는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransferRemovalSession>> open(const TransferAttempt& attempt) = 0;
    /// 제거 세션의 보호된 두 파일 볼륨을 승인 계약과 비교하는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransferRemovalSession>> openApproved(const TransferAttempt&, const MoveContract&) {
        return Result<std::unique_ptr<ITransferRemovalSession>>::failure({"transfer.contract_unsupported", "Approved volume handles are required."});
    }
};

/// 최종 복사본이 기록된 정방향/역방향 전송 원본 제거와 완료 기록을 조율한다.
class FinalizeTransferUseCase final {
public:
    /// 외부에서 소유한 기록·파일 제거·계획 검증 포트를 빌려 받는 함수.
    FinalizeTransferUseCase(ITransferJournal& journal, ITransferSourceRemoval& removal,
        const IHistoryRepository& history, const IPlanHasher& hasher);
    /// 다시 검증한 전송만 제거 의도·실제 제거·완료 기록 순서로 처리하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId,
        const std::function<bool()>& cancelled = {});
private:
    ITransferJournal& journal_;
    ITransferSourceRemoval& removal_;
    const IHistoryRepository& history_;
    const IPlanHasher& hasher_;
};
} // namespace nookbit
