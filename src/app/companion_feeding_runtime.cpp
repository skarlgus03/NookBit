#include "app/companion_feeding_runtime.hpp"
#include "modules/feeding/feeding_interaction.hpp"

namespace nookbit {
// 주입받은 창구에 등록하고 그 성공 여부만 소유하는 생성자.
CompanionFeedingRuntime::CompanionFeedingRuntime(IInteractionRegistry& registry)
    : registry_(registry), result_(registerFeedingInteraction(registry)) {}
// 동명 등록 충돌로 시작하지 못한 경우에는 다른 소유자의 종류를 제거하지 않는 소멸자.
CompanionFeedingRuntime::~CompanionFeedingRuntime() {
    if (result_) registry_.unregisterType("companion.feed");
}
}
