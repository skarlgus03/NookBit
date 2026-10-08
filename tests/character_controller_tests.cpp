#include "presentation/character_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <atomic>

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
} // namespace

// 캐릭터 상태와 작업 알림 연결 검사를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testStatePriority();
    testRunnerConnection();
    testIndependentSources();
    return failures == 0 ? 0 : 1;
}
