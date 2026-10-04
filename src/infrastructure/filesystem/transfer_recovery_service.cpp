#include "windows_transfer_recovery.hpp"
#include "operation_mutex.hpp"

namespace nookbit {
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
