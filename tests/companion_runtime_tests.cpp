#include "modules/character/behavior_manager.hpp"
#include "modules/character/interaction_manager.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int failures = 0;
// 기대한 행동 선택이나 세션 결과와 다르면 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 행동 조건·동률·발신자 독립성을 검사하는 함수.
void testBehaviors() {
    nookbit::BehaviorManager manager;
    expect(!manager.current(), "empty registry has no behavior");
    expect(manager.registerBehavior({"rest", "resting", 0, {}, {}}).hasValue(), "fallback registers");
    expect(manager.registerBehavior({"sit", "sit", 5, {"quiet"}, {"work"}}).hasValue(), "conditional behavior registers");
    expect(manager.registerBehavior({"work", "working", 10, {"work"}, {}}).hasValue(), "work behavior registers");
    expect(manager.registerBehavior({"alternate", "alternate", 10, {"work"}, {}}).hasValue(), "equal priority behavior registers");
    expect(!manager.registerBehavior({"sit", "other", 99, {}, {}}), "duplicate cannot replace definition");
    expect(!manager.registerBehavior({"bad", "bad", 0, {"x"}, {"x"}}), "contradictory conditions rejected");
    expect(manager.current()->id == "rest", "fallback selected without input");
    expect(manager.publish({"environment", "quiet", {"quiet"}, true}).hasValue() && manager.current()->id == "sit", "custom behavior selected by data");
    expect(manager.publish({"first", "job", {"work"}, true}).hasValue() && manager.current()->id == "work", "priority and registration order deterministic");
    expect(manager.publish({"second", "job", {"work"}, true}).hasValue(), "other work source registers");
    expect(manager.publish({"first", "job", {}, false}).hasValue() && manager.current()->id == "work", "one source cannot clear another");
    expect(manager.publish({"second", "job", {}, false}).hasValue() && manager.current()->id == "sit", "finished work reveals current environment");
    expect(!manager.publish({"", "bad", {"work"}, true}) && manager.current()->id == "sit", "invalid update leaves state unchanged");
}
// 새 상호작용을 추가해도 공통 관리자를 바꾸지 않고 처리되는지 검사하는 함수.
void testInteractions() {
    using namespace nookbit;
    InteractionManager manager;
    expect(registerBasicCompanionInteractions(manager).hasValue(), "basic definitions register");
    manager.setTargetAvailable("a", true);
    manager.setTargetAvailable("b", true);
    int calls = 0, cancels = 0;
    InteractionContext receivedContext;
    expect(manager.registerType({"feature.custom", "feature", "Custom", InteractionMode::Continuous},
        [&](const InteractionRequest& request) {
            // 발신 상황과 취소 처리가 개별 기능까지 전달되는지 검사하는 함수.
            ++calls;
            receivedContext = request.context;
            if (request.phase == InteractionPhase::Cancel) ++cancels;
            return Result<InteractionReaction>::success({"feature.reaction", "custom", request.phase == InteractionPhase::End ? 100 : 0});
        }).hasValue(), "additional feature registers its own handler");
    InteractionRequest request{1, "feature.custom", "a", InteractionPhase::Update, {"resting", {}}, InteractionPoint{1, 2}, {}};
    expect(manager.submit(request).reason == "interaction.no_session" && calls == 0, "update without begin cannot execute");
    request.phase = InteractionPhase::Begin;
    expect(manager.submit(request).status == InteractionStatus::Accepted && calls == 1, "begin accepted once");
    expect(manager.submit(request).status == InteractionStatus::Rejected && calls == 1, "repeated begin has no repeated effect");
    request.phase = InteractionPhase::Update;
    request.context.behaviorId = "working";
    expect(manager.submit(request).status == InteractionStatus::Accepted && receivedContext.behaviorId == "resting", "session retains original context");
    InteractionRequest other{1, "companion.drag", "b", InteractionPhase::Begin, {}, {}, {}};
    expect(manager.submit(other).status == InteractionStatus::Accepted, "different targets are independent");
    request.phase = InteractionPhase::End;
    expect(manager.submit(request).status == InteractionStatus::Completed, "end closes session");
    const int completedCalls = calls;
    expect(manager.submit(request).status == InteractionStatus::Rejected && calls == completedCalls, "late end cannot repeat effect");
    request.phase = InteractionPhase::Begin;
    expect(manager.submit(request).reason == "interaction.stale_request", "closed identifier cannot reopen");
    request.instanceId = 2;
    expect(manager.submit(request).status == InteractionStatus::Accepted, "new gesture may begin");
    expect(manager.setEnabled("feature.custom", false).hasValue() && cancels == 1, "disable invokes cleanup once");
    expect(manager.submit(request).reason == "interaction.disabled", "disabled type rejects new input");
    other.phase = InteractionPhase::End;
    expect(manager.submit(other).status == InteractionStatus::Completed, "disabling one type preserves other targets");
    expect(manager.setEnabled("feature.custom", true).hasValue(), "type reenabled");
    request.instanceId = 3;
    expect(manager.submit(request).status == InteractionStatus::Accepted, "new gesture after enabling");
    manager.setTargetAvailable("a", false);
    expect(cancels == 2 && manager.submit(request).reason == "interaction.target_unavailable", "hidden target cancels and blocks input");
    manager.setTargetAvailable("a", true);
    request.instanceId = 4;
    request.point = InteractionPoint{std::numeric_limits<double>::quiet_NaN(), 0};
    expect(manager.submit(request).reason == "interaction.invalid_input", "nonfinite point cannot reach handler");
    request = {4, "companion.pet", "a", InteractionPhase::Begin, {}, {}, {}};
    expect(manager.submit(request).reason == "interaction.point_required", "type validates required payload");
    request = {5, "companion.click", "a", InteractionPhase::Invoke, {"working", {}}, {}, {}};
    auto click = manager.submit(request);
    expect(click.status == InteractionStatus::Completed && click.reaction && click.reaction->durationMs > 0, "single click returns finite reaction");
    expect(manager.submit(request).reason == "interaction.stale_request", "single effect cannot execute twice");
    request.instanceId = 6;
    request.typeId = "missing";
    expect(manager.submit(request).reason == "interaction.unknown_type", "unregistered interaction rejected");
    int failedCleanups = 0;
    expect(manager.registerType({"test.failing", "test", "Failing", InteractionMode::Continuous},
        [&failedCleanups](const InteractionRequest& input) -> Result<InteractionReaction> {
            // 시작 중 예외가 나더라도 취소 정리가 호출되는지 검사하는 함수.
            if (input.phase == InteractionPhase::Cancel) { ++failedCleanups; return Result<InteractionReaction>::success({}); }
            throw std::runtime_error("effect failed");
        }).hasValue(), "failing feature registers");
    request = {6, "test.failing", "a", InteractionPhase::Begin, {}, {}, {}};
    expect(manager.submit(request).reason == "interaction.handler_failed" && failedCleanups == 1, "failed begin is cleaned up without throwing");
    request = {7, "companion.click", "a", InteractionPhase::Invoke, {}, {}, {}};
    expect(manager.submit(request).status == InteractionStatus::Completed, "failed begin does not leave target busy");
    expect(manager.registerType({"test.infinite", "test", "Invalid", InteractionMode::Once}, [](const InteractionRequest&) {
        // 종료 경로가 없는 단발 반응을 거절하는지 검사하는 함수.
        return Result<InteractionReaction>::success({"test.infinite", "invalid", 0});
    }).hasValue(), "invalid-response feature registers");
    request = {8, "test.infinite", "a", InteractionPhase::Invoke, {}, {}, {}};
    expect(manager.submit(request).reason == "interaction.invalid_reaction", "finished input cannot leave an infinite reaction");
}
}
// Qt 없이 행동 선택과 상호작용 세션의 회귀 검사를 실행하는 함수.
int main() {
    testBehaviors();
    testInteractions();
    return failures ? 1 : 0;
}
