#pragma once
#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 완료된 전송 결과를 원본으로 삼아 같은 안전 절차로 역전송하는 유스케이스.
class RollbackTransferUseCase final {
public:
    /// 외부에서 소유한 전송·검증·기록 포트를 빌려 받는 함수.
    RollbackTransferUseCase(ITransferJournal& journal, IHistoryRepository& history,
        IStagedCopy& copy, ITransferSourceRemoval& removal, ITransferFileInspector& files,
        const IPlanHasher& hasher);
    /// 원래 위치가 비어 있고 이동 결과가 그대로인 단계만 새 시도로 되돌리는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& forwardAttemptId,
        const std::string& rollbackAttemptId, const std::filesystem::path& staging,
        const std::function<bool()>& cancelled = {});
private:
    ITransferJournal& journal_;
    IHistoryRepository& history_;
    IStagedCopy& copy_;
    ITransferSourceRemoval& removal_;
    ITransferFileInspector& files_;
    const IPlanHasher& hasher_;
};
} // namespace nookbit
