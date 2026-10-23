#include "presentation/character_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include "infrastructure/configuration/json_character_presentation.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <QTemporaryDir>
#include <QFile>

namespace {
int failures = 0;
// 예상과 다르면 검사 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 화면 이벤트를 처리하며 제한된 시간 안에 작업 완료를 기다리는 함수.
bool waitForIdle(nookbit::BackgroundTaskRunner& runner) {
    QElapsedTimer timeout;
    timeout.start();
    while (runner.busy() && timeout.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return !runner.busy();
}
// 테스트 활동 상태를 구체 컨트롤러를 모르는 공통 창구로 전달하는 함수.
void reportWork(nookbit::ICharacterChannel& channel, bool active) {
    expect(channel.publish({"test-work", "operation", nookbit::CharacterUpdateKind::Activity, active, {}}).hasValue(),
           "activity should be accepted through the interface");
}
// 테스트 주의 요청을 구체 컨트롤러를 모르는 공통 창구로 전달하는 함수.
void reportAttention(nookbit::ICharacterChannel& channel, bool active) {
    expect(channel.publish({"test-watch", "proposal", nookbit::CharacterUpdateKind::Attention, active, {}}).hasValue(),
           "attention should be accepted through the interface");
}
// 작업 중 우선순위와 숨겨진 제안 상태·동일 상태 알림을 검사하는 함수.
void testStatePriority() {
    nookbit::CharacterController character;
    int changes = 0;
    QObject::connect(&character, &nookbit::CharacterController::stateChanged,
        &character, [&] { ++changes; });
    expect(character.stateKey() == "resting" && !character.message().isEmpty(), "initial state must rest");
    reportAttention(character, true);
    expect(character.stateKey() == "calling", "proposal must call the user");
    reportWork(character, true);
    expect(character.stateKey() == "working", "file work must override a proposal");
    reportAttention(character, false);
    reportAttention(character, true);
    reportWork(character, true);
    expect(changes == 2, "hidden proposal changes must not emit redundant visible-state changes");
    reportWork(character, false);
    expect(character.stateKey() == "calling", "remaining proposal must return after file work");
    reportAttention(character, false);
    expect(character.stateKey() == "resting", "accepted or snoozed proposal must return to rest");
}
// 별도 스레드의 성공·예외와 읽기 전용 검사가 캐릭터 상태를 올바르게 바꾸는지 검사하는 함수.
void testRunnerConnection() {
    nookbit::CharacterController character;
    nookbit::BackgroundTaskRunner runner;
    QObject receiver;
    QObject::connect(&runner, &nookbit::BackgroundTaskRunner::busyChanged, &character,
        [&] { reportWork(character, runner.fileWorkActive()); });
    reportAttention(character, true);
    QSemaphore gate;
    expect(runner.submit(&receiver, [&] { gate.acquire(); return nookbit::Result<void>::success(); },
        [](const auto&) {}), "inspection should start");
    expect(runner.busy() && !runner.fileWorkActive() && character.stateKey() == "calling",
           "read-only inspection must not select the work animation");
    gate.release();
    expect(waitForIdle(runner), "inspection should finish");
    expect(runner.submit(&receiver, [&] { gate.acquire(); return nookbit::Result<void>::success(); },
        [](const auto&) {}, nookbit::BackgroundTaskRunner::TaskKind::FileWork), "file work should start");
    expect(runner.fileWorkActive() && character.stateKey() == "working", "file work must select working immediately");
    expect(!runner.submit(&receiver, [] { return nookbit::Result<void>::success(); }, [](const auto&) {}) &&
               character.stateKey() == "working", "rejected duplicate must not change active task kind");
    gate.release();
    expect(waitForIdle(runner) && character.stateKey() == "calling", "success must return to the remaining proposal");
    bool failed = false;
    expect(runner.submit(&receiver, []() -> nookbit::Result<void> { throw std::runtime_error("test failure"); },
        [&](const auto& result) { failed = !result; }, nookbit::BackgroundTaskRunner::TaskKind::FileWork),
        "failing file work should start");
    reportAttention(character, false);
    expect(waitForIdle(runner) && failed && character.stateKey() == "resting" && !runner.fileWorkActive(),
           "failure must finish working and use the current proposal state");
}
// 여러 기능의 같은 항목 이름이 충돌하지 않고 문구·상태가 독립적으로 갱신되는지 검사하는 함수.
void testIndependentSources() {
    nookbit::CharacterController character;
    nookbit::ICharacterChannel& channel = character;
    using Kind = nookbit::CharacterUpdateKind;
    expect(channel.publish({"feature-a", "shared-id", Kind::Attention, true, "확인 요청 A"}).hasValue(),
           "a generic feature should publish attention");
    expect(channel.publish({"feature-b", "shared-id", Kind::Activity, true, "처리 중 B"}).hasValue(),
           "another generic feature should publish work independently");
    expect(channel.publish({"feature-a", "shared-id", Kind::Activity, true, "처리 중 A"}).hasValue(),
           "one source may own both activity and attention with the same entry id");
    expect(channel.publish({"feature-a", "shared-id", Kind::Activity, false, {}}).hasValue() &&
               character.stateKey() == "working" && character.message() == QString::fromUtf8("처리 중 B"),
           "stopping one feature must preserve another active feature and its message");
    expect(channel.publish({"feature-b", "shared-id", Kind::Activity, false, {}}).hasValue() &&
               character.stateKey() == "calling", "work completion must reveal retained attention");
    expect(channel.publish({"feature-a", "shared-id", Kind::Attention, true, "갱신된 요청"}).hasValue() &&
               character.message() == QString::fromUtf8("갱신된 요청"),
           "republishing the same identity must update its payload rather than add a duplicate");
    expect(!channel.publish({{}, "shared-id", Kind::Attention, false, {}}) &&
               character.stateKey() == "calling", "invalid identity must leave existing states unchanged");
    expect(!channel.publish({"feature-a", "shared-id", static_cast<Kind>(99), false, {}}) &&
               character.stateKey() == "calling", "unsupported kinds must fail without changing existing states");
    std::atomic<bool> rejected{false};
    auto* worker = QThread::create([&] {
        const auto result = channel.publish({"feature-a", "shared-id", Kind::Attention, false, {}});
        rejected = !result && result.error().code == "character.wrong_thread";
    });
    worker->start();
    worker->wait();
    delete worker;
    expect(rejected && character.stateKey() == "calling", "worker calls must be rejected before touching UI state");
    expect(channel.publish({"feature-a", "shared-id", Kind::Attention, false, {}}).hasValue() &&
               character.stateKey() == "resting", "removing the final request must return to rest");
}
// 프로젝트의 캐릭터 설정 예제 경로를 만드는 함수.
QString profilePath(const char* relative) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/" + QString::fromUtf8(relative);
}
// 실제 JSON 예제로 상황 선택·문구 우선순위·잘못된 참조를 검사하는 함수.
void testPresentationConfiguration() {
    const auto schema = profilePath("schemas/character-presentation.schema.json");
    const auto defaults = nookbit::loadCharacterPresentation(
        profilePath("resources/character/default.presentation.json"), schema);
    expect(defaults.hasValue(), "bundled test sprite presentation should validate");
    const auto sample = nookbit::loadCharacterPresentation(
        profilePath("resources/character/example.sprite.presentation.json"), schema);
    expect(sample.hasValue(), "sprite metadata example should validate without pretending its PNG exists");
    if (!sample) return;
    nookbit::CharacterController character;
    expect(character.setPresentation(sample.value()).hasValue() && character.animationId() == "idle",
           "resting should select the configured sprite animation");
    expect(character.setPresentation(sample.value(), QUrl("file:///preview/")).hasValue(),
           "preview fixture should have an independent image base");
    const auto previewState = character.animationId();
    const auto previews = character.previewAnimations();
    expect(previews.size() == static_cast<qsizetype>(sample.value().animations.size()),
           "preview must expose every authored animation");
    for (const auto& preview : previews) {
        const auto entry = preview.toMap();
        expect(!entry.value("descriptor").toMap().isEmpty() && !entry.value("label").toString().isEmpty(),
               "preview entries must include labels and playable descriptors");
    }
    expect(character.animationId() == previewState && character.stateKey() == "resting",
           "reading preview choices must not change the desktop companion state");
    auto custom = sample.value();
    custom.cues.push_back({"other.feature.offer", "별도 기능의 안내", "call"});
    expect(character.setPresentation(custom).hasValue(), "new situations should be configurable without state code changes");
    nookbit::ICharacterChannel& channel = character;
    expect(channel.publish({"other-feature", "offer", nookbit::CharacterUpdateKind::Attention, true, {},
                "other.feature.offer"}).hasValue() && character.cueId() == "other.feature.offer" &&
                character.animationId() == "call" && character.message() == QString::fromUtf8("별도 기능의 안내"),
           "a generic feature cue should choose its configured text and animation");
    expect(channel.publish({"other-feature", "offer", nookbit::CharacterUpdateKind::Attention, true, "동적 안내",
                "other.feature.offer"}).hasValue() && character.message() == QString::fromUtf8("동적 안내"),
           "explicit dynamic messages should override configured static text");
    expect(channel.publish({"other-feature", "offer", nookbit::CharacterUpdateKind::Attention, true, {},
                "not.configured"}).hasValue() && character.cueId() == "calling" && character.animationId() == "call",
           "unknown situations should fall back to the current generic state");
    auto invalid = custom;
    invalid.animations.front().frames = {12};
    expect(!character.setPresentation(invalid) && character.animationId() == "call",
           "invalid frame references must not replace a working profile");
    invalid = custom; invalid.animations.front().sheetId = "missing";
    expect(!nookbit::validateCharacterPresentation(invalid), "unknown sheets must fail");
    invalid = custom; invalid.cues.back().animationId = "missing";
    expect(!nookbit::validateCharacterPresentation(invalid), "unknown animations must fail");
    invalid = custom; invalid.sheets.front().image = "../outside.png";
    expect(!nookbit::validateCharacterPresentation(invalid), "parent traversal paths must fail");
    invalid = custom; invalid.sheets.front().image = "C:/outside.png";
    expect(!nookbit::validateCharacterPresentation(invalid), "absolute sheet paths must fail");
    invalid = custom; invalid.cues.push_back(invalid.cues.front());
    expect(!nookbit::validateCharacterPresentation(invalid), "duplicate situation identifiers must fail");
    invalid = custom; invalid.cues.erase(invalid.cues.begin());
    expect(!nookbit::validateCharacterPresentation(invalid), "required fallback situations must exist");
    QTemporaryDir directory;
    expect(directory.isValid(), "malformed profile fixture directory should be available");
    if (!directory.isValid()) return;
    QFile malformed(directory.filePath("invalid.json"));
    expect(malformed.open(QIODevice::WriteOnly), "malformed profile fixture should open");
    if (!malformed.isOpen()) return;
    malformed.write("{\"schemaVersion\":999,\"sheets\":[],\"animations\":[],\"cues\":[]}");
    malformed.close();
    expect(!nookbit::loadCharacterPresentation(malformed.fileName(), schema),
           "unsupported profile versions must fail schema validation");
}
} // namespace

// 캐릭터 상태와 작업 알림 연결 검사를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testStatePriority();
    testRunnerConnection();
    testIndependentSources();
    testPresentationConfiguration();
    return failures == 0 ? 0 : 1;
}
