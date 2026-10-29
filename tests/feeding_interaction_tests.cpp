#include "app/companion_feeding_runtime.hpp"
#include "presentation/companion_interaction_controller.hpp"
#include "presentation/character_controller.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVariantMap>
#include <iostream>

namespace {
int failures{};
// 기능 등록·반응·복귀가 기대와 다르면 원인을 남기는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 메뉴에 제공된 먹이 주기 동작이 현재 실행 가능한지 반환하는 함수.
bool canFeed(const nookbit::CompanionInteractionController& controller) {
    const auto actions = controller.menuActions();
    return actions.size() == 1 && actions[0].toMap().value("id") == "companion.feed" && actions[0].toMap().value("enabled").toBool();
}
}
// 임시 반응 외에 파일 작업·성장 수치·별도 실행기를 만들지 않는 먹이 주기를 검사하는 함수.
int main(int argc, char** argv) {
    using namespace nookbit;
    QCoreApplication app(argc, argv);
    JsonImagePackLoader loader(QStringLiteral(NOOKBIT_SOURCE_DIR "/schemas/imagepack.schema.json"));
    const auto pack = loader.load(std::filesystem::path(QStringLiteral(NOOKBIT_SOURCE_DIR "/resources/imagepacks/nmoo/imagepack.json").toStdWString()));
    if (!pack) { std::cerr << pack.error().message; return 1; }
    CharacterController character;
    expect(bool(character.setImagePack(pack.value())), "configure bundled pack without eating artwork");
    InteractionManager manager;
    expect(bool(registerBasicCompanionInteractions(manager)), "basic gestures still register separately");
    CompanionInteractionController controller(manager, "primary", [&] {
        // 표시 담당자의 상태를 값으로 조회하는 함수.
        return InteractionContext{character.behaviorId().toStdString(), character.reactionId().toStdString()};
    });
    QObject::connect(&controller, &CompanionInteractionController::reactionRequested, &character, &CharacterController::presentInteraction);
    QObject::connect(&character, &CharacterController::stateChanged, &controller, &CompanionInteractionController::refreshContext);
    QObject::connect(&character, &CharacterController::reactionChanged, &controller, &CompanionInteractionController::refreshContext);
    controller.setAvailable(true);
    expect(controller.menuActions().isEmpty() && !controller.invoke("companion.feed", 0, 0), "module absent means no menu and no handler");
    {
        CompanionFeedingRuntime feeding(manager);
        expect(bool(feeding.startupResult()) && canFeed(controller), "module contributes a generic enabled menu action");
        {
            CompanionFeedingRuntime duplicate(manager);
            expect(!duplicate.startupResult(), "duplicate registration fails explicitly");
        }
        expect(canFeed(controller), "failed duplicate cannot remove another owner's registration");
        const auto restingAnimation = character.animationId();
        expect(controller.invoke("companion.feed", 1, 2) && !character.reactionId().isEmpty()
            && character.message().contains(QStringLiteral("냠냠")) && character.animationId() == restingAnimation,
            "feeding uses fallback artwork with its own short message");
        const auto reaction = character.reactionId();
        expect(!canFeed(controller) && !controller.invoke("companion.feed", 0, 0) && character.reactionId() == reaction,
            "repeated feeding cannot interrupt or restart the current reaction");
        QEventLoop deadline;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &deadline, &QEventLoop::quit);
        QObject::connect(&character, &CharacterController::reactionChanged, &deadline, [&] {
            // 실제 반응 타이머가 끝나면 검사 대기를 종료하는 함수.
            if (character.reactionId().isEmpty()) deadline.quit();
        });
        timeout.start(4000);
        deadline.exec();
        expect(character.reactionId().isEmpty() && character.behaviorId() == "resting" && canFeed(controller), "deadline returns to rest and re-enables feeding");
        expect(bool(character.registerBehavior({"walking.left", "walk.left", 5, {"walk.left"}, {"working", "attention"}})), "register walking behavior");
        expect(bool(character.publish({"walk-test", "walk", {"walk.left"}, true})), "walk through behavior channel");
        expect(controller.invoke("companion.feed", 0, 0), "walking can accept feeding");
        controller.cancel();
        expect(character.behaviorId() == "walking.left" && character.animationId() == "walk", "cancel resumes the current walking behavior");
        auto custom = pack.value();
        custom.bindings["interact.feed"] = "notice";
        expect(bool(character.setImagePack(custom)) && controller.invoke("companion.feed", 0, 0) && character.animationId() == "notice",
            "pack binding can provide eating artwork without changing the module");
        controller.cancel();
        ICharacterChannel& activity = character;
        expect(bool(activity.publish({"file-work", "operation", CharacterUpdateKind::Activity, true, {}, "file.work"})), "publish work fact");
        expect(!canFeed(controller) && !controller.invoke("companion.feed", 0, 0) && character.behaviorId() == "working", "work disables feeding without changing work state");
        expect(bool(activity.publish({"file-work", "operation", CharacterUpdateKind::Activity, false, {}, "file.work"})), "withdraw work fact");
        expect(bool(activity.publish({"notice", "offer", CharacterUpdateKind::Attention, true, {}, "file.offer"})), "publish attention fact");
        expect(!canFeed(controller) && !controller.invoke("companion.feed", 0, 0), "attention disables feeding");
        expect(bool(activity.publish({"notice", "offer", CharacterUpdateKind::Attention, false, {}, "file.offer"})), "withdraw attention fact");
        controller.setAvailable(false);
        expect(!canFeed(controller) && !controller.invoke("companion.feed", 0, 0), "hidden companion rejects feeding");
        controller.setAvailable(true);
        expect(controller.invoke("companion.feed", 0, 0), "shown companion accepts fresh feeding");
        controller.setAvailable(false);
        expect(character.reactionId().isEmpty(), "hiding cancels timed reaction");
        controller.setAvailable(true);
    }
    expect(controller.menuActions().isEmpty() && !controller.invoke("companion.feed", 0, 0), "module teardown removes menu and handler");
    expect(bool(manager.registerType({"test.guard", "test", "Guard", InteractionMode::Once, true},
        [](const InteractionRequest&) { return Result<InteractionReaction>::success({}); },
        [](const InteractionContext&) -> bool { throw 1; })), "register exception guard");
    expect(!controller.menuActions()[0].toMap().value("enabled").toBool() && !controller.invoke("test.guard", 0, 0), "availability exceptions reject safely");
    return failures ? 1 : 0;
}
