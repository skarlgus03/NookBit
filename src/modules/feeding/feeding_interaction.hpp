#pragma once
#include <nookbit/application/companion_interaction.hpp>

namespace nookbit {
/// 먹이 주기 종류와 반응·가능 조건만 외부 상호작용 창구에 등록하는 함수.
Result<void> registerFeedingInteraction(IInteractionRegistry& registry);
}
