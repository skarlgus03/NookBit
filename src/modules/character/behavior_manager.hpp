#pragma once
#include <nookbit/application/companion_behavior.hpp>
#include <map>
#include <optional>
#include <vector>

namespace nookbit {
/// 기본 행동 등록과 상황 선택만 담당하는 Qt 없는 관리자.
class BehaviorManager final : public IBehaviorChannel {
public:
    /// 조건과 식별자를 검사하여 행동을 등록하는 함수.
    [[nodiscard]] Result<void> registerBehavior(CompanionBehavior behavior);
    /// 발신자별 상황 입력을 갱신하는 함수.
    [[nodiscard]] Result<void> publish(const BehaviorUpdate& update) override;
    /// 조건을 만족하는 가장 높은 우선도의 행동을 고르는 함수. 동률은 등록 순서다.
    [[nodiscard]] std::optional<CompanionBehavior> current() const;
private:
    std::vector<CompanionBehavior> behaviors_;
    std::map<std::pair<std::string, std::string>, std::set<std::string>> inputs_;
};
}
