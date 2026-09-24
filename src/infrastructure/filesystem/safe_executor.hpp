#pragma once

#include <nookbit/application/ports.hpp>

#include <filesystem>

namespace nookbit {

/// 승인된 한 파일 계획만 기록과 재검사를 거쳐 이동·되돌리는 첫 실행기.
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

    /// DB에 승인된 단일 파일 계획을 덮어쓰기 없이 실행하는 함수.
    [[nodiscard]] Result<void> execute(
        const OperationPlan& plan,
        const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selectedFiles) override;

    /// 완료된 단일 파일 계획을 원래 위치로 안전하게 되돌리는 함수.
    [[nodiscard]] Result<void> rollback(const PlanId& planId) override;

private:
    ISettingsRepository& settings_;
    IRuleSetRepository& rules_;
    IScanner& scanner_;
    IPlanner& planner_;
    IPlanHasher& hasher_;
    IHistoryRepository& history_;
    std::filesystem::path appDataRoot_;
};

} // namespace nookbit
