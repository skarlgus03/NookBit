#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/staged_copy.hpp>
#include <nookbit/application/transfer_journal.hpp>

namespace nookbit {
/// 중단된 임시 복사본만 보호 검증·이어쓰기·전체 검증하며 확정/원본 제거는 하지 않는다.
class ResumePartialCopyUseCase final {
public:
    /// 외부 소유의 기록·복사·서명 검증 포트를 빌려 받는 함수.
    ResumePartialCopyUseCase(const IHistoryRepository& history, ITransferJournal& journal, IStagedCopy& copy, const IPlanHasher& hasher);
    /// 승인과 앞부분 검증 후 남은 부분만 복사하고 전체 검증 결과를 기록하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    IStagedCopy& copy_;
    const IPlanHasher& hasher_;
};
}
