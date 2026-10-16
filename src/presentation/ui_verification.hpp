#pragma once
#include <QString>

class QObject;
class QQuickWindow;
class QQuickItem;
namespace nookbit { class BackgroundTaskRunner; class PendingFilesModel; class UiSettingsController; }

// 실제 앱 화면을 사용하는 자동 검사의 진입점. 개인 데이터는 앱 조립부에서 격리한다.
bool verifyBundledHistorySchema(const QString& migrationPath);
bool verifyExtensionInteraction(QQuickWindow* mainWindow, QQuickWindow* extension, QObject* metrics);
bool verifyInlineFilePreview(QQuickWindow* window, QQuickItem* page);
bool verifyHomeMenuAlignment(QQuickWindow* root, QObject* metrics);
bool verifyBusyPresentation(QQuickWindow* root, QQuickWindow* extension, nookbit::BackgroundTaskRunner& runner);
bool verifyPageFileDrop(QQuickWindow* window, QQuickItem* page, nookbit::PendingFilesModel& pending);
bool captureVerificationScreens(QQuickWindow* root, QObject* metrics, nookbit::UiSettingsController& settings, const QString& directory);
