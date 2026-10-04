#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/staged_copy.hpp>
#include <nookbit/application/transfer_journal.hpp>

namespace nookbit {
/// 전체 검증된 재개 복사본의 최종 이름만 확정하며 원본 제거는 별도 작업에 맡긴다.
class CommitResumedStagingUseCase final {
public:
    /// 외부 소유의 기록·복사·서명 검사 포트를 빌려 받는 함수.
    CommitResumedStagingUseCase(const IHistoryRepository& history, ITransferJournal& journal,
        IStagedCopy& copy, const IPlanHasher& hasher);
    /// 보호 재검증과 의도 기록 뒤 덮어쓰기 없이 확정하고 최종 증거를 저장하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    IStagedCopy& copy_;
    const IPlanHasher& hasher_;
};
}
