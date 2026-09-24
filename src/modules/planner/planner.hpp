#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 파일별 분류 결과를 검증된 읽기 전용 이동 초안으로 바꾸는 구현체.
class Planner final : public IPlanner {
public:
    /// 규칙 판단기와 보호할 앱 데이터 폴더를 받는 생성자.
    Planner(IRuleEngine& ruleEngine, std::filesystem::path appDataRoot);

    /// 규칙·경로·충돌을 검사해 승인 전 미리보기 초안을 만드는 함수.
    [[nodiscard]] Result<OperationPlan> buildPlan(
        const ScanSnapshot& snapshot,
        const UserSettings& settings,
        const RuleSet& rules) override;

private:
    IRuleEngine& ruleEngine_;
    std::filesystem::path appDataRoot_;
};

} // namespace nookbit
