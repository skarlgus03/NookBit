#include "interrupted_operation_recovery_service.hpp"
#include "operation_mutex.hpp"
namespace nookbit {
// 시작 조회와 달리 명시적 선택만 공유 시스템 잠금 안에서 실행하는 함수.
Result<void> InterruptedOperationRecoveryService::execute(const RecoveryRequest& request) {
    if (request.action == RecoveryAction::Later) return Result<void>::success();
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return acquired;
    return useCase_.execute(request);
}
}
