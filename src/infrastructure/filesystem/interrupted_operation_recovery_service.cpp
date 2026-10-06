#include "interrupted_operation_recovery_service.hpp"
#include "operation_mutex.hpp"
namespace nookbit {
// 공유 잠금 아래 진단과 실행 전 검사로 선택 후보를 생성하는 함수.
Result<std::vector<InterruptedOperationView>> InterruptedOperationRecoveryService::inspectAll() {
    using Return = Result<std::vector<InterruptedOperationView>>;
    if (!inspector_ || !history_) return Return::failure({"recovery.inspector_missing", "Recovery inspection is not wired."});
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return Return::failure(acquired.error());
    const auto reports = inspector_->inspectAll();
    if (!reports) return Return::failure(reports.error());
    std::vector<InterruptedOperationView> result;
    for (const auto& report : reports.value()) {
        const auto plan = history_->loadPlan(report.planId);
        if (!plan) return Return::failure(plan.error());
        InterruptedOperationView item;
        item.inspection = report; item.planHash = plan.value().planHash;
        item.formatVersion = plan.value().formatVersion;
        const auto resume = useCase_.checkChoice({report.planId, item.planHash, RecoveryAction::Resume});
        const auto rollback = useCase_.checkChoice({report.planId, item.planHash, RecoveryAction::Rollback});
        item.canResume = resume.hasValue(); item.canRollback = rollback.hasValue();
        if (!resume) item.resumeBlock = resume.error();
        if (!rollback) item.rollbackBlock = rollback.error();
        result.push_back(std::move(item));
    }
    return Return::success(std::move(result));
}
// 시작 조회와 달리 명시적 선택만 공유 시스템 잠금 안에서 실행하는 함수.
Result<void> InterruptedOperationRecoveryService::execute(const RecoveryRequest& request) {
    if (request.action == RecoveryAction::Later) return Result<void>::success();
    OperationMutex lock;
    const auto acquired = lock.acquire();
    if (!acquired) return acquired;
    return useCase_.execute(request);
}
}
