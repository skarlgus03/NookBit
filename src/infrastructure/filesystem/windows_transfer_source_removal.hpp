#pragma once
#include <nookbit/application/finalize_transfer_use_case.hpp>
namespace nookbit {
/// 임의 경로 삭제가 아닌 검증된 전송 원본 제거만 제공하는 어댑터.
class WindowsTransferSourceRemoval final : public ITransferSourceRemoval {
public:
    /// 확정 증거와 원본·최종 파일을 검증해 보호 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferRemovalSession>> open(const TransferAttempt& attempt) override;
    /// 보호된 원본·복사본의 볼륨이 승인 계약과 일치할 때만 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferRemovalSession>> openApproved(const TransferAttempt& attempt, const MoveContract& movement) override;
private:
    /// 계약 유무에 따라 동일한 보호 제거 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferRemovalSession>> openImpl(const TransferAttempt& attempt, const MoveContract* movement);
};
} // namespace nookbit
