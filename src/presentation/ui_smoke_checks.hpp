#pragma once

class QApplication;
class QIcon;
class QMenu;
class QObject;
class QQmlApplicationEngine;
class QSettings;

namespace nookbit {
class AppLifecycleController;
class FeaturePreferences;
class FeatureRuntimeHost;
class FeatureSettingsController;
class TutorialController;
class UiPageRegistry;
class UserGuideController;
struct FeatureSelection;
struct FeatureStartupPlan;
}

namespace nookbit::ui_smoke {

bool verifyApplicationIcon(const QIcon& icon);

// 바인딩 경고는 이벤트 처리 중에도 들어오므로 검사 종료까지 같은 상태를 참조한다.
int verifyTutorial(QQmlApplicationEngine& engine, FeatureRuntimeHost& features, const UiPageRegistry& uiPages,
    QSettings& uiPreferences, TutorialController& tutorial, bool verifyTutorialSkip, const bool& featureBindingsClean);
int verifyFeatureStartup(QQmlApplicationEngine& engine, FeatureRuntimeHost& features, const UiPageRegistry& uiPages,
    const FeatureStartupPlan& startup, TutorialController& tutorial, const QMenu& trayMenu,
    FeatureSettingsController& featureSettings, FeaturePreferences& featurePreferences,
    const FeatureSelection& savedFeatures, bool verifyFeaturePreferences, const bool& featureBindingsClean);
int verifyUi(QApplication& application, QQmlApplicationEngine& engine, FeatureRuntimeHost& features,
    QObject* uiMetrics, AppLifecycleController& appLifecycle, const UserGuideController& userGuides);

} // namespace nookbit::ui_smoke
