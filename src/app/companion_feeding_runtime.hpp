#pragma once
#include <nookbit/application/companion_interaction.hpp>

namespace nookbit {
/// 다른 기능의 작업자 없이 먹이 주기 등록의 수명만 소유하는 모듈.
class CompanionFeedingRuntime final {
public:
    /// 더 오래 사는 상호작용 등록 창구를 빌려 기능을 추가하는 생성자.
    explicit CompanionFeedingRuntime(IInteractionRegistry& registry);
    /// 직접 등록에 성공한 종류만 해제하는 소멸자.
    ~CompanionFeedingRuntime();
    /// 등록 실패를 호출자에게 돌려주는 함수.
    const Result<void>& startupResult() const { return result_; }
    CompanionFeedingRuntime(const CompanionFeedingRuntime&) = delete;
    CompanionFeedingRuntime& operator=(const CompanionFeedingRuntime&) = delete;
private:
    IInteractionRegistry& registry_;
    Result<void> result_;
};
}
