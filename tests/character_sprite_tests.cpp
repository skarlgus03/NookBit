#include "presentation/character_controller.hpp"
#include "infrastructure/configuration/json_character_presentation.hpp"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <memory>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
// 예상과 다르면 검사 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 테스트용 소스·시트의 절대 경로를 만드는 함수.
QString projectPath(const char* relative) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/" + QString::fromUtf8(relative);
}
// 화면 없이 이벤트를 처리하며 이미지 로딩이나 프레임 변경을 기다리는 함수.
bool waitFor(const std::function<bool()>& ready, int limitMs = 2000) {
    QElapsedTimer timeout;
    timeout.start();
    while (!ready() && timeout.elapsed() < limitMs) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}
// 프레임 번호를 반환하는 함수.
int frame(QObject& sprite) { return sprite.property("currentFrame").toInt(); }
// 실제 PNG와 QML 재생기로 상태 전환·순서·정지·반복·잘못된 이미지를 검사하는 함수.
void testPlayback() {
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(projectPath("src/presentation/qml/CharacterSprite.qml")));
    expect(component.isReady(), "sprite component should load");
    if (!component.isReady()) { std::cerr << component.errorString().toStdString(); return; }
    std::unique_ptr<QObject> sprite(component.create());
    expect(sprite != nullptr, "sprite component should instantiate without a window");
    if (!sprite) return;
    sprite->setProperty("width", 64);
    sprite->setProperty("height", 64);
    sprite->setProperty("playing", false);
    const auto profile = nookbit::loadCharacterPresentation(projectPath("resources/character/default.presentation.json"),
        projectPath("schemas/character-presentation.schema.json"));
    expect(profile.hasValue(), "test sprite profile should validate");
    if (!profile) return;
    nookbit::CharacterController character;
    QObject::connect(&character, &nookbit::CharacterController::animationChanged, sprite.get(),
        [&] { sprite->setProperty("descriptor", character.animation()); });
    expect(character.setPresentation(profile.value(),
        QUrl::fromLocalFile(projectPath("resources/character/"))).hasValue(), "sprite profile should configure");
    expect(waitFor([&] { return sprite->property("ready").toBool(); }), "actual PNG should load with declared grid dimensions");
    QImage image(projectPath("resources/character/sprites/test-character.png"));
    expect(image.size() == QSize(512, 384) && image.pixelColor(0, 0).alpha() == 0,
           "fixture atlas should have the exact grid dimensions and transparent background");
    expect(frame(*sprite) == 0, "resting should begin at frame zero");
    for (int index = 1; index <= 4; ++index) {
        QMetaObject::invokeMethod(sprite.get(), "advanceFrame");
        expect(frame(*sprite) == index % 4, "idle should follow declared frames and wrap");
    }
    nookbit::ICharacterChannel& channel = character;
    expect(channel.publish({"watch", "offer", nookbit::CharacterUpdateKind::Attention, true, {}, "file.offer"}).hasValue(),
           "watch cue should be accepted");
    expect(frame(*sprite) == 4, "calling should reset to its first declared frame");
    auto* atlas = sprite->findChild<QObject*>("characterAtlas");
    expect(atlas && atlas->property("x").toDouble() == 0 && atlas->property("y").toDouble() == -64,
           "second sprite row must crop from its scaled grid position");
    expect(channel.publish({"work", "job", nookbit::CharacterUpdateKind::Activity, true, {}, "file.work"}).hasValue() &&
               frame(*sprite) == 8, "work should override calling and begin on row three");
    QMetaObject::invokeMethod(sprite.get(), "advanceFrame");
    const auto beforeMessage = frame(*sprite);
    expect(channel.publish({"work", "job", nookbit::CharacterUpdateKind::Activity, true, "진행 상황", "file.work"}).hasValue() &&
               frame(*sprite) == beforeMessage, "text-only updates must not restart the animation");
    expect(channel.publish({"work", "job", nookbit::CharacterUpdateKind::Activity, true, {}, "working"}).hasValue() &&
               frame(*sprite) == 8, "a different situation using the same clip must restart playback");
    expect(channel.publish({"work", "job", nookbit::CharacterUpdateKind::Activity, false, {}, {}}).hasValue() &&
               frame(*sprite) == 4, "work completion should restore the pending attention animation");
    expect(channel.publish({"watch", "offer", nookbit::CharacterUpdateKind::Attention, false, {}, {}}).hasValue() &&
               frame(*sprite) == 0, "removing attention should restore idle");
    auto descriptor = character.animation();
    descriptor["frames"] = QVariantList{1, 3, 1};
    descriptor["loop"] = false;
    sprite->setProperty("descriptor", descriptor);
    expect(frame(*sprite) == 1, "arbitrary frame lists should begin at their first entry");
    QMetaObject::invokeMethod(sprite.get(), "advanceFrame");
    expect(frame(*sprite) == 3, "noncontiguous frame order should be honored");
    QMetaObject::invokeMethod(sprite.get(), "advanceFrame");
    QMetaObject::invokeMethod(sprite.get(), "advanceFrame");
    expect(frame(*sprite) == 1 && sprite->property("finished").toBool(), "one-shot playback should hold its last frame");
    descriptor = character.animation();
    descriptor["frameDurationMs"] = 20;
    sprite->setProperty("descriptor", descriptor);
    sprite->setProperty("playing", true);
    expect(waitFor([&] { return frame(*sprite) != 0; }, 500), "timer should advance visible playback");
    sprite->setProperty("playing", false);
    const auto pausedFrame = frame(*sprite);
    QElapsedTimer paused;
    paused.start();
    waitFor([&] { return paused.elapsed() >= 80; }, 100);
    expect(frame(*sprite) == pausedFrame, "paused playback should not keep advancing");
    sprite->setProperty("visible", false);
    sprite->setProperty("playing", true);
    auto* timer = sprite->findChild<QObject*>("characterSpriteTimer");
    expect(timer && !timer->property("running").toBool(), "hidden sprite must stop its frame timer");
    sprite->setProperty("visible", true);
    descriptor["frames"] = QVariantList{2};
    sprite->setProperty("descriptor", descriptor);
    expect(frame(*sprite) == 2 && timer && !timer->property("running").toBool(), "a still image must not run a frame timer");
    descriptor["frameWidth"] = 64;
    sprite->setProperty("descriptor", descriptor);
    expect(!sprite->property("ready").toBool() && !sprite->property("error").toString().isEmpty(),
           "wrong actual image dimensions must block playback and report an error");
    descriptor = character.animation();
    descriptor["source"] = QUrl::fromLocalFile(projectPath("resources/character/sprites/missing-test.png"));
    sprite->setProperty("descriptor", descriptor);
    expect(waitFor([&] { return !sprite->property("error").toString().isEmpty(); }) && !sprite->property("ready").toBool(),
           "missing images must block playback and report an error");
}
} // namespace

// 화면을 띄우지 않고 캐릭터 스프라이트 재생 검사를 실행하는 함수.
int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    testPlayback();
    return failures == 0 ? 0 : 1;
}
