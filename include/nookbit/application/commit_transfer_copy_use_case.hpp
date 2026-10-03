#pragma once

#include <nookbit/application/staged_copy.hpp>
#include <nookbit/application/transfer_journal.hpp>

namespace nookbit {

/// 복사와 기록을 조율하되 원본 제거·상위 이동 완료는 수행하지 않는 유스케이스.
class CommitTransferCopyUseCase final {
public:
    /// 외부에서 만든 복사 일꾼과 기록 포트를 빌려 받는 함수.
    CommitTransferCopyUseCase(IStagedCopy& copy, ITransferJournal& journal);
    /// 새 시도를 기록하고 검증된 복사본 확정 단계까지만 수행하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const TransferAttempt& attempt,
        const std::function<bool()>& cancelled = {});
private:
    IStagedCopy& copy_;
    ITransferJournal& journal_;
};

} // namespace nookbit
