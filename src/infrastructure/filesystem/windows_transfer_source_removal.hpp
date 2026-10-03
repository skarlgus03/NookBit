#pragma once
#include <nookbit/application/finalize_transfer_use_case.hpp>
namespace nookbit {
/// 임의 경로 삭제가 아닌 검증된 전송 원본 제거만 제공하는 어댑터.
class WindowsTransferSourceRemoval final : public ITransferSourceRemoval {
public:
    /// 확정 증거와 원본·최종 파일을 검증해 보호 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferRemovalSession>> open(const TransferAttempt& attempt) override;
};
} // namespace nookbit
