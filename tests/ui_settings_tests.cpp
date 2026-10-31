#include "presentation/ui_settings_controller.hpp"
#include "presentation/destination_settings_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

// 앱 테마·페이지 크기의 재실행 복원과 자동 검사 시 무저장을 검사하는 함수.
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
    const QSize fallback(860, 680);
    if (first.extensionSize(1, fallback) != fallback) return 1;
    first.rememberExtensionSize(1, 1200, 800);
    first.rememberExtensionSize(8, 560, 520);
    first.rememberExtensionSize(11, 620, 600);
    first.rememberExtensionSize(12, 700, 780);
    first.rememberExtensionSize(14, 580, 680);
    QSettings reopened(directory.filePath("ui.ini"), QSettings::IniFormat);
    nookbit::UiSettingsController sized(reopened, true);
    if (sized.extensionSize(11, fallback) != QSize(620, 600)) return 1;
    if (sized.extensionSize(12, fallback) != QSize(700, 780)) return 1;
    if (sized.extensionSize(14, fallback) != QSize(580, 680)) return 1;
    if (sized.extensionSize(1, fallback) != QSize(1200, 800)
        || sized.extensionSize(8, fallback) != QSize(560, 520)
        || sized.extensionSize(7, fallback) != fallback) return 1;
    sized.rememberExtensionSize(1, -20, 700);
    sized.rememberExtensionSize(1, 20000, 700);
    sized.rememberExtensionSize(99, 700, 600);
    if (sized.extensionSize(1, fallback) != QSize(1200, 800)
        || sized.extensionSize(99, fallback) != fallback) return 1;
    reopened.setValue("windows/history/size", QString("broken"));
    reopened.setValue("windows/preview/size", QSize(50000, 100));
    if (sized.extensionSize(4, fallback) != fallback || sized.extensionSize(2, fallback) != fallback) return 1;
    if (dryRun.extensionSize(1, fallback) != fallback) return 1;
    dryRun.rememberExtensionSize(1, 900, 700);
    if (dryRun.extensionSize(1, fallback) != QSize(900, 700)
        || reopened.value("windows/ruleEditor/size").value<QSize>() != QSize(1200, 800)) return 1;
    // 외부 허용의 재실행 복원·해제와 검사 모드의 무저장을 확인한다.
    const auto home = directory.filePath("home");
    const auto external = directory.filePath("external");
    nookbit::DestinationSettingsController destinations(settings, home, true);
    if (!destinations.allowedRoots().isEmpty() || destinations.allowFolder("relative") ||
        destinations.allowFolder(QDir::rootPath()) || !destinations.allowFolder(external)) return 1;
    nookbit::DestinationSettingsController remembered(reopened, home, true);
    if (remembered.allowedRoots() != QStringList{external} ||
        remembered.relativeToHome(home + "/Pictures") != "Pictures" ||
        !remembered.relativeToHome(external).isEmpty()) return 1;
    nookbit::DestinationSettingsController memoryOnly(settings, home, false);
    if (!memoryOnly.allowedRoots().isEmpty() || !memoryOnly.allowFolder(directory.filePath("dry"))) return 1;
    if (settings.value("destinations/allowedRoots").toStringList() != QStringList{external}) return 1;
    remembered.revokeFolder(external);
    nookbit::DestinationSettingsController revoked(settings, home, true);
    if (!revoked.allowedRoots().isEmpty()) return 1;
    std::cout << "UI and destination permission persistence passed\n";
    return 0;
}
