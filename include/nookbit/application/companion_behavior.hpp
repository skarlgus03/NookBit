#pragma once
#include <nookbit/domain/result.hpp>
#include <set>
#include <string>

namespace nookbit {
/// 상황 태그로 선택하는 기본 행동. 애니메이션 파일이나 실제 업무를 소유하지 않는다.
struct CompanionBehavior {
    std::string id;
    std::string cueId;
    int priority{0};
    std::set<std::string> requiredTags;
    std::set<std::string> blockedTags;
};

/// 한 기능이 제공하는 상황 입력. 같은 발신자·항목은 갱신하거나 해제한다.
struct BehaviorUpdate {
    std::string sourceId;
    std::string entryId;
    std::set<std::string> tags;
    bool active{false};
};

/// 컴패니언의 기본 행동에 필요한 사실만 전달하는 입력 창구.
class IBehaviorChannel {
public:
    /// 인터페이스를 통해 수신 구현을 안전하게 해제하는 소멸자.
    virtual ~IBehaviorChannel() = default;
    /// 수신 구현의 실행 문맥에서 상황 입력을 갱신하는 함수.
    [[nodiscard]] virtual Result<void> publish(const BehaviorUpdate& update) = 0;
};
}
