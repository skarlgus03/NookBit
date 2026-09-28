#pragma once

#include <nookbit/application/ports.hpp>
#include <nookbit/application/use_cases/verify_saved_plan.hpp>

namespace nookbit {

/// 저장된 계획이 현재 파일·설정·규칙과 여전히 일치하는지 읽기 전용으로 검사한다.
class VerifySavedPlanUseCase final : public IVerifySavedPlanUseCase {
public:
    /// 이력 읽기와 현재 상태 재검사에 필요한 기능을 받는 생성자.
    VerifySavedPlanUseCase(IRuleSetRepository& rules,
                           IScanner& scanner,
                           IPlanner& planner,
                           IPlanHasher& hasher,
                           IHistoryRepository& history);

    /// DB에서 계획을 다시 읽고 해시와 현재 계획을 비교하는 함수.
    [[nodiscard]] Result<OperationPlan> execute(
        const PlanId& planId,
        const std::vector<std::filesystem::path>& selectedFiles,
        const UserSettings& settings,
        PlanStatus expectedStatus = PlanStatus::Planned) override;

private:
    IRuleSetRepository& rules_;
    IScanner& scanner_;
    IPlanner& planner_;
    IPlanHasher& hasher_;
    IHistoryRepository& history_;
};

} // namespace nookbit
