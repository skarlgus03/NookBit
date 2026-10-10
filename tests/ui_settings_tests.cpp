#include "presentation/ui_settings_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

// 앱 테마 저장·재실행 복원과 자동 검사 시 무저장을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings settings(directory.filePath("ui.ini"), QSettings::IniFormat);
    nookbit::UiSettingsController first(settings, true);
    if (first.darkMode()) return 1;
    first.setDarkMode(true);
    nookbit::UiSettingsController restored(settings, true);
    if (!restored.darkMode()) return 1;
    nookbit::UiSettingsController dryRun(settings, false);
    if (dryRun.darkMode()) return 1;
    dryRun.setDarkMode(true); dryRun.setDarkMode(false);
    if (!settings.value("appearance/darkMode").toBool()) return 1;
    restored.setDarkMode(false);
    nookbit::UiSettingsController light(settings, true);
    if (light.darkMode()) return 1;
    std::cout << "UI theme persistence passed\n";
    return 0;
}
