#pragma once

#include <nookbit/application/ports.hpp>

#include <filesystem>

namespace nookbit {

/// 승인된 계획의 파일들을 순서대로 이동하고 안전하게 되돌리는 실행기.
class SafeExecutor final : public IExecutor {
public:
    /// 현재 상태 검증과 이력 저장에 필요한 기능을 받는 생성자.
    SafeExecutor(ISettingsRepository& settings,
                 IRuleSetRepository& rules,
                 IScanner& scanner,
                 IPlanner& planner,
                 IPlanHasher& hasher,
                 IHistoryRepository& history,
                 std::filesystem::path appDataRoot);

    /// 승인된 계획의 파일들을 덮어쓰기 없이 순서대로 실행하는 함수.
    [[nodiscard]] Result<void> execute(
        const OperationPlan& plan,
        const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selectedFiles) override;

    /// 완료된 계획의 이동 파일들을 역순으로 되돌리는 함수.
    [[nodiscard]] Result<void> rollback(const PlanId& planId) override;

private:
    /// 이미 적용된 단계들을 검증하고 역순으로 되돌려 최종 상태를 기록하는 함수.
    [[nodiscard]] Result<void> rollbackAppliedSteps(
        const OperationPlan& plan,
        const std::vector<const MoveStep*>& applied,
        PlanStatus finalStatus,
        bool duringExecution);

    /// 확인 불가 상태를 기록하고 작업을 중단하는 함수.
    [[nodiscard]] Result<void> markNeedsRecovery(const PlanId& planId);

    ISettingsRepository& settings_;
    IRuleSetRepository& rules_;
    IScanner& scanner_;
    IPlanner& planner_;
    IPlanHasher& hasher_;
    IHistoryRepository& history_;
    std::filesystem::path appDataRoot_;
};

} // namespace nookbit
