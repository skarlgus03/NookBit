#include "windows_transfer_recovery.hpp"
#include "operation_mutex.hpp"

namespace nookbit {
// 다른 실행·복구와 같은 시스템 잠금 안에서 재개 복사본을 확정하는 함수.
Result<TransferAttempt> ResumedStagingCommitService::execute(const std::string& attemptId, const std::function<bool()>& cancelled) {
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return Result<TransferAttempt>::failure(acquired.error());
    return useCase_.execute(attemptId, cancelled);
}
// 다른 실행·복구와 같은 시스템 잠금 안에서 부분 복사 재개를 요청하는 함수.
Result<TransferAttempt> PartialCopyResumeService::execute(const std::string& attemptId, const std::function<bool()>& cancelled) {
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return Result<TransferAttempt>::failure(acquired.error());
    return useCase_.execute(attemptId, cancelled);
}
// 다른 실행·되돌리기·복구와 공유하는 시스템 잠금 안에서 전송을 마무리하는 함수.
Result<TransferAttempt> CommittedTransferResumeService::execute(const std::string& attemptId, const std::function<bool()>& cancelled) {
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return Result<TransferAttempt>::failure(acquired.error());
    return useCase_.execute(attemptId, cancelled);
}
// 실행·되돌리기와 공유하는 시스템 잠금 아래 완료 기록만 복구하는 함수.
Result<TransferAttempt> TransferRecoveryService::execute(const std::string& attemptId) {
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return Result<TransferAttempt>::failure(acquired.error());
    return useCase_.execute(attemptId);
}
// 다른 실행·복구와 공유하는 잠금을 획득한 뒤 임시 복사본 정리를 요청하는 함수.
Result<TransferAttempt> StagingCleanupService::execute(const std::string& attemptId) {
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return Result<TransferAttempt>::failure(acquired.error());
    return useCase_.execute(attemptId);
}
} // namespace nookbit
