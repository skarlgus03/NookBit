#include "presentation/app_lifecycle_controller.hpp"
#include <QCoreApplication>
#include <iostream>

namespace {
int failures = 0;
// 종료 정책이 기대와 다르면 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 트레이 닫기·복원·실행 중 종료 차단·트레이 유실을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    nookbit::AppLifecycleController lifecycle;
    int hides = 0, shows = 0, exits = 0, blocked = 0;
    QObject::connect(&lifecycle, &nookbit::AppLifecycleController::hideRequested, [&hides] {
        // 창 숨기기 요청 개수를 기록하는 함수.
        ++hides;
    });
    QObject::connect(&lifecycle, &nookbit::AppLifecycleController::showRequested, [&shows] {
        // 창 복원 요청 개수를 기록하는 함수.
        ++shows;
    });
    QObject::connect(&lifecycle, &nookbit::AppLifecycleController::exitRequested, [&exits] {
        // 완전 종료 요청 개수를 기록하는 함수.
        ++exits;
    });
    QObject::connect(&lifecycle, &nookbit::AppLifecycleController::exitBlocked, [&blocked] {
        // 작업 중 종료 차단 개수를 기록하는 함수.
        ++blocked;
    });
    lifecycle.setTrayAvailable(true);
    lifecycle.closeWindow();
    expect(hides == 1 && exits == 0, "close hides but never quits with tray");
    lifecycle.setBusy(true);
    lifecycle.closeWindow();
    expect(hides == 2 && exits == 0 && blocked == 0, "busy work continues while main window hides");
    lifecycle.requestExit();
    expect(exits == 0 && blocked == 1 && shows == 1, "busy exit rejected and progress window restored");
    lifecycle.setBusy(false);
    lifecycle.openWindow();
    expect(shows == 2, "open restores main window");
    lifecycle.requestExit();
    expect(exits == 1, "explicit idle exit allowed");
    lifecycle.setTrayAvailable(false);
    expect(shows == 3, "tray loss restores access to main window");
    lifecycle.closeWindow();
    expect(exits == 2 && hides == 2, "without tray close exits instead of stranding app");
    lifecycle.setBusy(true);
    lifecycle.closeWindow();
    expect(exits == 2 && blocked == 2, "fallback also protects running operations");
    return failures ? 1 : 0;
}
