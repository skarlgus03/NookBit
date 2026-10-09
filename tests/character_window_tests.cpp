#include "presentation/character_window_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>

namespace {
int failures = 0;
// 기대한 위치·상태와 다르면 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 위치 보정·모니터 제거·숨기기·개인 위치 저장을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QList<QRect> screens{QRect(0, 0, 1920, 1040), QRect(-1280, -200, 1280, 960)};
    nookbit::CharacterWindowController window(screens, QPoint(-500, 200), true);
    expect(window.anchorX() == -500 && window.anchorY() == 200, "negative second-screen position preserved");
    window.moveAnchor(99999, -99999);
    expect(window.anchorX() == 1887 && window.anchorY() == 32, "offscreen position restored inside nearest screen");
    window.moveAnchor(-500, 200);
    window.setScreens({screens.first()});
    expect(window.anchorX() == 32 && window.anchorY() == 200, "removed monitor returns character to remaining screen");
    window.setShown(false);
    expect(!window.shown(), "hide affects only display state");
    window.resetPosition();
    expect(window.anchorX() == 1739 && window.anchorY() == 899, "reset uses primary work area");
    QTemporaryDir folder;
    expect(folder.isValid(), "owned preferences folder created");
    const auto file = folder.filePath("window.ini");
    {
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("anchor", QPoint(window.anchorX(), window.anchorY()));
        settings.setValue("shown", window.shown());
        settings.sync();
        expect(settings.status() == QSettings::NoError, "position saved");
    }
    QSettings restored(file, QSettings::IniFormat);
    nookbit::CharacterWindowController reopened(screens, restored.value("anchor").toPoint(), restored.value("shown").toBool());
    expect(reopened.anchorX() == 1739 && reopened.anchorY() == 899 && !reopened.shown(), "position and hidden state restored");
    int updates = 0;
    QObject::connect(&reopened, &nookbit::CharacterWindowController::positionChanged, [&updates] {
        // 동일 위치의 반복 저장이 발생하지 않는지 알림을 세는 함수.
        ++updates;
    });
    reopened.moveAnchor(1739, 899);
    expect(updates == 0, "same position produces no save loop");
    reopened.setShown(true);
    expect(reopened.shown(), "hidden character can be shown again");
    return failures ? 1 : 0;
}
