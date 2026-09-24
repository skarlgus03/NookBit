#pragma once

#include "application/use_cases/verify_saved_plan_use_case.hpp"

namespace nookbit {

/// 사용자의 명시적 요청 시 재검증된 계획에만 승인 기록을 남기는 유스케이스.
class ApprovePlanUseCase final {
public:
    /// 재검증과 이력 저장 기능을 받는 생성자.
    ApprovePlanUseCase(VerifySavedPlanUseCase& verifier, IHistoryRepository& history);

    /// 저장 계획을 다시 확인하고 승인 시각과 해시를 기록하는 함수.
    [[nodiscard]] Result<ApprovalRecord> execute(
        const PlanId& planId,
        const std::vector<std::filesystem::path>& selectedFiles);

private:
    VerifySavedPlanUseCase& verifier_;
    IHistoryRepository& history_;
};

} // namespace nookbit
