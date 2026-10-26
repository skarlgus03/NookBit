#include "modules/character/idle_walk_state.hpp"
#include "presentation/companion_motion_controller.hpp"
#include "presentation/character_controller.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "infrastructure/configuration/directory_image_pack_library.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
// 이동 범위나 중단 결과가 예상과 다르면 기록하는 함수.
void expect(bool value, const char* message) { if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
// 화면 없이 이벤트를 처리하며 조건을 기다리는 함수.
bool waitFor(const std::function<bool()>& condition, int limitMs = 1500) {
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < limitMs) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return condition();
}
// 시간 기반 속도·방향 전환·경계·수동 이동·지연 후 점프 방지를 검사하는 함수.
void testState() {
    using namespace nookbit;
    IdleWalkState state;
    expect(state.configure({1000, 1000, 40}).hasValue(), "valid config accepted");
    expect(!state.configure({0, 1000, 40}), "invalid config rejected");
    IdleWalkContext context{true, 100, 0, 200};
    expect(!state.advance(500, context).walking && state.nextDelayMs() == 500, "rest waits remaining time");
    expect(state.advance(500, context).walking, "rest transitions to walk");
    IdleWalkStep step;
    for (int i = 0; i < 10; ++i) { step = state.advance(100, context); context.x = std::round(step.x); }
    expect(!step.walking && std::abs(step.x - 60) < 0.001, "time determines travel distance and ends walk");
    expect(state.advance(1000, context).direction == 1, "next walk reverses direction after resting");
    const auto before = context.x;
    step = state.advance(10000, context);
    expect(!step.walking && std::abs(step.x - before) <= 4.001, "long pause cannot teleport character");
    context = {true, 40, 0, 100};
    state.reset();
    (void)state.advance(1000, context);
    context.allowed = false;
    expect(!state.advance(0, context).walking, "work or interaction interrupts immediately");
    context.allowed = true;
    expect(!state.advance(500, context).walking, "resuming starts fresh rest");
    (void)state.advance(500, context);
    context.x = 80;
    expect(!state.advance(16, context).walking, "external move cannot be pulled back to old path");
    expect(state.configure({100, 10000, 40}).hasValue(), "edge fixture configured");
    context = {true, 10, 0, 100};
    step = state.advance(100, context);
    expect(step.walking && step.direction == 1, "near edge chooses direction with room");
    for (int i = 0; i < 30 && step.walking; ++i) {
        step = state.advance(100, context); context.x = std::round(step.x);
        expect(step.x >= 0 && step.x <= 100, "walking stays inside bounds");
    }
    expect(!step.walking && step.x == 100, "reaching boundary ends walking");
}
// 실제 이미지 팩·방향·구버전 팩 호환을 검사하는 함수.
void testPack() {
    using namespace nookbit;
    const auto root = QString::fromUtf8(NOOKBIT_SOURCE_DIR);
    JsonImagePackLoader loader(root + "/schemas/imagepack.schema.json");
    const auto manifest = root + "/resources/imagepacks/nmoo/imagepack.json";
    auto pack = loader.load(std::filesystem::path(manifest.toStdWString()));
    expect(pack.hasValue(), "actual supplied PNG pack validates");
    if (!pack) return;
    const auto& steps = pack.value().animations.at("walk").steps;
    expect(steps.size() == 4 && steps[0].frameId == "walk-1" && steps[1].frameId == "walk-2" &&
        steps[2].frameId == "walk-3" && steps[3].frameId == "walk-2", "walk follows supplied frame cycle");
    CharacterController character;
    expect(character.setImagePack(pack.value()).hasValue() && character.canWalk(), "walk capability from bindings");
    expect(character.registerBehavior({"walking.right", "walk.right", 5, {"walk.right"}, {"working", "attention"}}).hasValue(), "walk registered as data");
    IBehaviorChannel& behaviors = character;
    expect(behaviors.publish({"motion", "path", {"walk.right"}, true}).hasValue() &&
        character.animationId() == "walk" && character.animation().value("mirrorX").toBool(), "shared clip mirrors right walk");
    auto directional = pack.value();
    directional.animations["walk-right"] = directional.animations.at("walk");
    directional.bindings["walk.right"] = "walk-right";
    expect(character.setImagePack(directional).hasValue() && !character.animation().value("mirrorX").toBool(), "authored right clip not mirrored twice");
    auto legacy = loader.load(std::filesystem::path((root + "/resources/imagepacks/test/imagepack.json").toStdWString()));
    expect(legacy && character.setImagePack(legacy.value()).hasValue() && !character.canWalk(), "old pack remains valid without walking");
    QTemporaryDir storage;
    DirectoryImagePackLibrary library(loader, root + "/resources/imagepacks/test/imagepack.json", storage.path(), {{"builtin-nmoo", manifest}});
    const auto list = library.list();
    expect(list && list.value().size() == 2 && library.load("builtin-nmoo").hasValue() && !library.load("builtin-unknown"), "additional builtins use explicit registry");
}
// 시간 어댑터의 중단·재개와 포인터 조작 우선을 검사하는 함수.
void testAdapter() {
    using namespace nookbit;
    IdleWalkContext context{true, 250, 0, 500};
    int moves = 0;
    CompanionMotionController controller([&] { return context; }, [&](const IdleWalkStep& step) {
        // 실제 창 없이 적용된 최신 좌표와 이동 횟수만 기록하는 함수.
        if (step.move) { context.x = std::round(step.x); ++moves; }
    }, true, {100, 300, 60});
    controller.refresh();
    expect(!controller.walking() && waitFor([&] { return controller.walking() && moves > 0; }), "timer starts walk after rest");
    controller.setPointerHeld(true);
    expect(!controller.walking(), "press interrupts before click or drag is resolved");
    const auto heldX = context.x;
    QElapsedTimer clock; clock.start();
    (void)waitFor([&] { return clock.elapsed() >= 150; });
    expect(context.x == heldX, "holding pointer prevents autonomous movement");
    controller.setPointerHeld(false);
    expect(!controller.walking() && waitFor([&] { return controller.walking(); }), "release starts fresh rest then walk");
    context.allowed = false;
    controller.refresh();
    expect(!controller.walking(), "work notice or hidden state stops motion");
    controller.setEnabled(false);
    context.allowed = true;
    controller.refresh();
    expect(!controller.walking(), "disabled module remains still");
    controller.setEnabled(true);
    expect(waitFor([&] { return controller.walking(); }), "enabling starts from rest");
    controller.restart();
    expect(!controller.walking(), "pack replacement drops old path");
    controller.setEnabled(false);
}
}
// 기본 걷기의 순수 계산·실제 팩·시간 어댑터를 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testState(); testPack(); testAdapter();
    return failures ? 1 : 0;
}
