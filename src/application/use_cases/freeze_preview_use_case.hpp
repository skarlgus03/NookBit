#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 미리보기의 입력을 재검사하고 변조 검출 해시와 함께 저장하는 유스케이스.
class FreezePreviewUseCase final {
public:
    /// 재검사·해시·저장에 필요한 외부 기능을 받는 생성자.
    FreezePreviewUseCase(ISettingsRepository& settings,
                         IRuleSetRepository& rules,
                         IScanner& scanner,
                         IPlanner& planner,
                         IPlanHasher& hasher,
                         IHistoryRepository& history);

    /// 화면 초안과 현재 상태가 같을 때만 Planned 계획으로 저장하는 함수.
    [[nodiscard]] Result<OperationPlan> execute(
        const OperationPlan& draft,
        const std::vector<std::filesystem::path>& selectedFiles);

private:
    ISettingsRepository& settings_;
    IRuleSetRepository& rules_;
    IScanner& scanner_;
    IPlanner& planner_;
    IPlanHasher& hasher_;
    IHistoryRepository& history_;
};

} // namespace nookbit
