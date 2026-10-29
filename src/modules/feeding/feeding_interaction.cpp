#include "modules/feeding/feeding_interaction.hpp"

namespace nookbit {
// 먹기 그림의 유무를 모르고 일시 반응 데이터만 반환하는 등록 함수.
Result<void> registerFeedingInteraction(IInteractionRegistry& registry) {
    return registry.registerType({"companion.feed", "companion-feeding", "먹이 주기", InteractionMode::Once, true},
        [](const InteractionRequest&) {
            // 파일 작업이나 수치 상태를 변경하지 않고 짧은 먹기 반응을 만드는 함수.
            return Result<InteractionReaction>::success({"interact.feed", "냠냠… 고마워요!", 2400});
        }, [](const InteractionContext& context) {
            // 다른 반응과 작업·알림을 끼어들어 중단하지 않도록 입력 가능 상황만 판단하는 함수.
            return context.reactionId.empty() && (context.behaviorId == "resting"
                || context.behaviorId == "walking.left" || context.behaviorId == "walking.right");
        });
}
}
