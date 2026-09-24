#include "application/use_cases/approve_plan_use_case.hpp"

#include <chrono>

namespace nookbit {

// 재검증과 이력 저장 기능을 받는 생성자.
ApprovePlanUseCase::ApprovePlanUseCase(VerifySavedPlanUseCase& verifier,
                                     IHistoryRepository& history)
    : verifier_(verifier), history_(history) {}

// 저장 계획을 다시 확인하고 승인 시각과 해시를 기록하는 함수.
Result<ApprovalRecord> ApprovePlanUseCase::execute(
    const PlanId& planId,
    const std::vector<std::filesystem::path>& selectedFiles) {
    auto checked = verifier_.execute(planId, selectedFiles);
    if (!checked) return Result<ApprovalRecord>::failure(checked.error());
    ApprovalRecord approval{checked.value().id, checked.value().planHash,
                            std::chrono::system_clock::now()};
    auto saved = history_.saveApproval(approval);
    if (!saved) return Result<ApprovalRecord>::failure(saved.error());
    return Result<ApprovalRecord>::success(std::move(approval));
}

} // namespace nookbit
