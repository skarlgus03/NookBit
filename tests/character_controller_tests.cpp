#include "presentation/character_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <iostream>
#include <stdexcept>

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
// 작업 중 우선순위와 숨겨진 제안 상태·동일 상태 알림을 검사하는 함수.
void testStatePriority() {
    nookbit::CharacterController character;
    int changes = 0;
    QObject::connect(&character, &nookbit::CharacterController::stateChanged,
        &character, [&] { ++changes; });
    expect(character.stateKey() == "resting" && !character.message().isEmpty(), "initial state must rest");
    character.setProposalAvailable(true);
    expect(character.stateKey() == "calling", "proposal must call the user");
    character.setWorking(true);
    expect(character.stateKey() == "working", "file work must override a proposal");
    character.setProposalAvailable(false);
    character.setProposalAvailable(true);
    character.setWorking(true);
    expect(changes == 2, "hidden proposal changes must not emit redundant visible-state changes");
    character.setWorking(false);
    expect(character.stateKey() == "calling", "remaining proposal must return after file work");
    character.setProposalAvailable(false);
    expect(character.stateKey() == "resting", "accepted or snoozed proposal must return to rest");
}
// 별도 스레드의 성공·예외와 읽기 전용 검사가 캐릭터 상태를 올바르게 바꾸는지 검사하는 함수.
void testRunnerConnection() {
    nookbit::CharacterController character;
    nookbit::BackgroundTaskRunner runner;
    QObject receiver;
    QObject::connect(&runner, &nookbit::BackgroundTaskRunner::busyChanged, &character,
        [&] { character.setWorking(runner.fileWorkActive()); });
    character.setProposalAvailable(true);
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
    character.setProposalAvailable(false);
    expect(waitForIdle(runner) && failed && character.stateKey() == "resting" && !runner.fileWorkActive(),
           "failure must finish working and use the current proposal state");
}
} // namespace

// 캐릭터 상태와 작업 알림 연결 검사를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testStatePriority();
    testRunnerConnection();
    return failures == 0 ? 0 : 1;
}
