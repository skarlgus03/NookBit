#include "presentation/companion_interaction_controller.hpp"
#include "presentation/character_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <iostream>

namespace {
int failures = 0;
// 표현 복귀나 입력 수명 검사에 실패하면 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 현재 행동·일시 반응·숨김·짧은 타이머의 연결을 검사하는 함수.
int main(int argc, char** argv) {
    using namespace nookbit;
    QCoreApplication app(argc, argv);
    CharacterController character;
    InteractionManager manager;
    expect(registerBasicCompanionInteractions(manager).hasValue(), "basic interactions register");
    expect(manager.registerType({"test.short", "test", "Short", InteractionMode::Once}, [](const InteractionRequest&) {
        // 실제 초 단위 대기 없이 만료 후 복귀를 검사하는 반응을 만드는 함수.
        return Result<InteractionReaction>::success({"test.short", "short", 20});
    }).hasValue(), "short test reaction registers");
    CompanionInteractionController controller(manager, "primary", [&character] {
        // 입력 시점의 상태만 전달하는 함수.
        return InteractionContext{character.behaviorId().toStdString(), character.reactionId().toStdString()};
    });
    QObject::connect(&controller, &CompanionInteractionController::reactionRequested, &character, &CharacterController::presentInteraction);
    expect(!controller.invoke("companion.click", 0, 0), "hidden target rejects input");
    controller.setAvailable(true);
    ICharacterChannel& activity = character;
    expect(activity.publish({"work", "job", CharacterUpdateKind::Activity, true, {}, "file.work"}).hasValue(), "file work begins");
    expect(controller.begin("companion.pet", 1, 2) && controller.active() && !character.reactionId().isEmpty(), "pet session overlays work");
    expect(character.behaviorId() == "working" && character.stateKey() == "working", "reaction preserves basic work state");
    expect(!controller.invoke("companion.click", 0, 0) && controller.active(), "concurrent input cannot replace gesture");
    expect(activity.publish({"work", "job", CharacterUpdateKind::Activity, false, {}, "file.work"}).hasValue() &&
        character.behaviorId() == "resting" && !character.reactionId().isEmpty(), "base state updates underneath reaction");
    controller.cancel();
    expect(!controller.active() && character.reactionId().isEmpty() && character.stateKey() == "resting", "cancel returns to latest state");
    expect(character.registerBehavior({"sit", "sit", 5, {"quiet"}, {"working"}}).hasValue(), "new behavior registered without UI change");
    IBehaviorChannel& behaviors = character;
    expect(behaviors.publish({"environment", "quiet", {"quiet"}, true}).hasValue() && character.behaviorId() == "sit", "new behavior driven by generic input");
    expect(controller.invoke("test.short", 0, 0) && !character.reactionId().isEmpty(), "short reaction shown");
    QElapsedTimer wait;
    wait.start();
    while (!character.reactionId().isEmpty() && wait.elapsed() < 2000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    expect(character.reactionId().isEmpty() && character.behaviorId() == "sit", "deadline returns to current custom behavior");
    expect(controller.begin("companion.drag", 0, 0) && controller.update(20, 30) && controller.end() && !controller.active(), "drag completes without a click");
    expect(!controller.end(), "completed gesture cannot end again");
    expect(controller.begin("companion.pet", 0, 0), "gesture begins before hide");
    controller.setAvailable(false);
    expect(!controller.active() && character.reactionId().isEmpty() && !controller.update(0, 0) && !controller.invoke("companion.click", 0, 0), "hide clears input and reaction");
    controller.setAvailable(true);
    std::atomic<bool> rejected{false};
    auto* worker = QThread::create([&] { rejected = !controller.begin("companion.pet", 0, 0); });
    worker->start(); worker->wait(); delete worker;
    expect(rejected && !controller.active(), "other thread cannot mutate UI state");
    return failures ? 1 : 0;
}
