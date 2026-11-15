#include "presentation/pending_files_model.hpp"
#include "app/feature_connections.hpp"
#include "app/feature_runtime_host.hpp"
#include "app/feature_preferences.hpp"
#include "presentation/feature_settings_controller.hpp"
#include "app/notification_runtime.hpp"
#include "app/folder_watch_runtime.hpp"
#include "app/file_organizer_runtime.hpp"
#include "app/companion_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include "app/companion_tray_action.hpp"
#include "presentation/file_icon_provider.hpp"
#include "presentation/file_preview_provider.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/ui_smoke_checks.hpp"

#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QQuickStyle>
#include <QPalette>
#include "presentation/ui_settings_controller.hpp"
#include "presentation/ui_font.hpp"
#include "presentation/tutorial_controller.hpp"
#include "presentation/destination_settings_controller.hpp"
#include "presentation/user_guide_controller.hpp"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include "presentation/app_lifecycle_controller.hpp"
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <memory>
#include <QQmlContext>
#include <QStandardPaths>
#include <QSettings>
#include "presentation/companion_edit_controller.hpp"
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <algorithm>
#include "presentation/background_task_runner.hpp"
#include "presentation/character_controller.hpp"
#include "presentation/notification_controller.hpp"
#include "presentation/companion_interaction_controller.hpp"
#include "presentation/companion_motion_controller.hpp"
#include "presentation/character_settings_controller.hpp"
#include "presentation/character_window_controller.hpp"
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QJSValue>

// 실행 파일에 내장된 여러 해상도의 공통 앱 아이콘을 읽는 함수.
static QIcon applicationIcon() {
    return QIcon(QStringLiteral(":/nookbit/icons/nookbit.ico"));
}

// 응용 프로그램과 의존성을 조립하고 이벤트 루프를 실행하는 함수.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    nookbit::configureUiTextRendering();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setOrganizationName(QStringLiteral("NookBitProject"));
    QCoreApplication::setApplicationName(QStringLiteral("NookBit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NOOKBIT_VERSION_STRING));
    const auto arguments = QCoreApplication::arguments();
    if (arguments.contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "NookBit " << QCoreApplication::applicationVersion() << Qt::endl;
        return EXIT_SUCCESS;
    }
    const bool verifyFeaturePreferences = arguments.contains(QStringLiteral("--verify-feature-preferences"));
    const bool verifyTutorialSkip = arguments.contains(QStringLiteral("--verify-tutorial-skip"));
    const bool verifyTutorial = verifyTutorialSkip || arguments.contains(QStringLiteral("--verify-tutorial"));
    const bool verifyFeatures = verifyTutorial || verifyFeaturePreferences || arguments.contains(QStringLiteral("--verify-feature-startup"));
    const bool verifyUi = verifyFeatures || arguments.contains(QStringLiteral("--verify-ui"));
    const auto selection = nookbit::parseFeatureSelection(arguments);
    if (!selection) { QTextStream(stderr) << QString::fromStdString(selection.error().message) << Qt::endl; return EXIT_FAILURE; }
    application.setWindowIcon(applicationIcon());
    if (verifyUi && !nookbit::ui_smoke::verifyApplicationIcon(application.windowIcon())) return EXIT_FAILURE;
    // 숨긴 메인 창과 도구 창의 개수로 프로세스 수명을 결정하지 않는다.
    application.setQuitOnLastWindowClosed(false);
    nookbit::AppLifecycleController appLifecycle;
    nookbit::UserGuideController userGuides;

    std::unique_ptr<QTemporaryDir> isolatedFeatureData;
    if (verifyFeatures) {
        isolatedFeatureData = std::make_unique<QTemporaryDir>();
        if (!isolatedFeatureData->isValid()) return EXIT_FAILURE;
    }
    const auto appData = verifyFeatures ? isolatedFeatureData->path() : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto fallbackDestination = QDir(documents).filePath(QStringLiteral("NookBit Organized"));
    auto preferenceStorage = verifyFeatures
        ? std::make_unique<QSettings>(QDir(appData).filePath("preferences.ini"), QSettings::IniFormat)
        : std::make_unique<QSettings>();
    auto& uiPreferences = *preferenceStorage;
    nookbit::FeaturePreferences featurePreferences(uiPreferences);
    // 저장 복원 검사에서는 임시 INI에만 선택을 먼저 기록한 뒤 실제 시작 읽기 경로를 사용한다.
    if (verifyFeaturePreferences && !featurePreferences.save(selection.value())) return EXIT_FAILURE;
    // 자동 검사는 개인 모듈 설정을 읽거나 쓰지 않고 기존 전체 기능 기준을 유지한다.
    const auto loadedFeatures = verifyUi && !verifyFeaturePreferences
        ? nookbit::Result<nookbit::FeatureSelection>::success({}) : featurePreferences.load();
    const auto savedFeatures = loadedFeatures ? loadedFeatures.value() : nookbit::FeatureSelection{false, false, false, false, false};
    const auto selectedFeatures = nookbit::parseFeatureSelection(verifyFeaturePreferences ? QStringList{} : arguments, savedFeatures);
    auto startup = nookbit::resolveFeatureStartup(selectedFeatures.value());
    const auto featureLoadError = loadedFeatures ? QString() : QString::fromStdString(loadedFeatures.error().message);
    if (!featureLoadError.isEmpty()) startup.messages.prepend(featureLoadError);
    if (verifyUi && !verifyFeatures && startup.active != nookbit::FeatureSelection{}) {
        QTextStream(stderr) << "Use --verify-feature-startup for disabled feature combinations." << Qt::endl;
        return EXIT_FAILURE;
    }
    const bool featureOverride = !verifyFeaturePreferences && std::any_of(arguments.cbegin(), arguments.cend(),
        [](const QString& argument) { return argument.startsWith("--disable-feature="); });
    nookbit::FeatureSettingsController featureSettings(savedFeatures, startup.active, featureOverride,
        [&featurePreferences, verifyUi, verifyFeaturePreferences](nookbit::FeatureSelection value) {
            // 화면 자동 검사에서는 저장 콜백만 성공시키고 사용자 환경은 변경하지 않는 함수.
            return verifyUi && !verifyFeaturePreferences ? nookbit::Result<void>::success() : featurePreferences.save(value);
        }, featureLoadError);
    nookbit::UiSettingsController uiSettings(uiPreferences, !verifyUi);
    const auto rememberedDestination = verifyUi
        ? QString() : uiPreferences.value(QStringLiteral("destinationRoot")).toString().trimmed();
    const auto developmentDestination = QString::fromUtf8(NOOKBIT_DEV_DESTINATION_ROOT);
    // 로컬 개발 빌드에서 고정한 경로가 있으면 사용자 기억값보다 우선하는 기본 경로.
    const QFileInfo rememberedInfo(developmentDestination.isEmpty()
        ? rememberedDestination : developmentDestination);
    const auto defaultDestination = rememberedInfo.isAbsolute() &&
                                    !QDir(rememberedInfo.absoluteFilePath()).isRoot()
        ? rememberedInfo.absoluteFilePath() : fallbackDestination;
    if (QCoreApplication::arguments().contains(QStringLiteral("--print-default-destination"))) {
        QTextStream(stdout) << QDir::toNativeSeparators(defaultDestination) << Qt::endl;
        return EXIT_SUCCESS;
    }
    nookbit::UiPageRegistry uiPages({2, 8, 9, 12, 13, 14});
    const auto watchFolder = verifyFeatures ? QDir(appData).filePath("watch")
        : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (verifyFeatures && startup.active.watch) QDir().mkpath(watchFolder);
    nookbit::FeatureRuntimeHost features(startup, {appData, defaultDestination,
        QCoreApplication::applicationDirPath(), watchFolder, verifyUi}, uiPreferences, application, uiPages);
    if (!features.startupResult()) {
        QTextStream(stderr) << QString::fromStdString(features.startupResult().error().message) << Qt::endl;
        return EXIT_FAILURE;
    }
    auto* organizer = features.organizer();
    auto* folderWatch = features.watch();
    auto* notificationRuntime = features.notifications();
    auto* companion = features.companion();
    nookbit::TutorialController tutorial(uiPreferences, !verifyUi || verifyTutorial);
    const auto tutorialText = [](const char* text) {
        // 기본 기능과 분리된 공통 사용 안내를 번역하는 함수.
        return QCoreApplication::translate("Tutorial", text);
    };
    const auto hostTutorial = uiPages.addTutorialSteps({
        {"host.welcome", "host", tutorialText("NookBit에 오신 걸 환영해요"),
            tutorialText("파일 정리부터 컴패니언까지, 사용법을 살펴봐요."),
            {tutorialText("메인 메뉴에서 기능을 선택하면 옆에 페이지가 열려요."),
             tutorialText("다음을 누르거나 아래 목차에서 궁금한 항목을 골라 보세요.")}, {}, 0},
        {"host.finish", "host", tutorialText("이제 직접 사용해 보세요"),
            tutorialText("작은 테스트 파일부터 시작해 보세요."),
            {tutorialText("파일 정리에 파일을 넣고 결과 미리보기를 확인해 보세요."),
             tutorialText("필요한 기능은 모듈 관리에서 선택할 수 있어요. 저장 후 앱을 완전히 종료했다 다시 켜면 적용돼요.")},
            tutorialText("이 안내는 설정 → 정보 → 튜토리얼 다시 보기에서 다시 열 수 있어요."), 1000}
    }, tutorial);
    if (!hostTutorial) {
        QTextStream(stderr) << QString::fromStdString(hostTutorial.error().message) << Qt::endl;
        return EXIT_FAILURE;
    }
    bool featureBindingsClean = true;
    QQmlApplicationEngine engine;
    // 테마와 컨트롤을 만들기 전에 공통 글꼴을 확정하고 창에서도 명시적으로 공유한다.
    auto* uiMetrics = engine.singletonInstance<QObject*>("NookBit", "UiMetrics");
    if (!uiMetrics || uiMetrics->property("textBody").toInt() <= 0) {
        QTextStream(stderr) << "Cannot load shared UI metrics." << Qt::endl;
        return EXIT_FAILURE;
    }
    if (!nookbit::loadApplicationUiFonts(*uiMetrics)) {
        QTextStream(stderr) << "Cannot load bundled UI fonts." << Qt::endl;
        return EXIT_FAILURE;
    }
    const auto baseFont = nookbit::applicationUiFont(*uiMetrics);
    application.setFont(baseFont);
    uiMetrics->setProperty("applicationFont", baseFont);
    if (verifyUi) QTextStream(stdout) << "UI font: " << baseFont.families().join(", ")
        << " weight=" << baseFont.weight() << " style=" << baseFont.styleName() << Qt::endl;
    auto* uiTheme = engine.singletonInstance<QObject*>("NookBit", "UiTheme");
    if (!uiTheme) {
        QTextStream(stderr) << "Cannot load shared UI theme." << Qt::endl;
        return EXIT_FAILURE;
    }
    const auto lightWidgetPalette = application.palette();
    // 네이티브 메뉴도 QML 공통 색상표에서 읽어 같은 테마를 적용하는 함수.
    const auto applyWidgetTheme = [&application, &uiSettings, uiTheme, lightWidgetPalette] {
        const auto colors = uiTheme->property(uiSettings.darkMode() ? "dark" : "light").value<QJSValue>().toVariant().toMap();
        QPalette palette = lightWidgetPalette;
        palette.setColor(QPalette::Window, QColor(colors.value("window").toString()));
        palette.setColor(QPalette::Base, QColor(colors.value("surface").toString()));
        palette.setColor(QPalette::AlternateBase, QColor(colors.value("alternate").toString()));
        palette.setColor(QPalette::Button, QColor(colors.value("button").toString()));
        palette.setColor(QPalette::Mid, QColor(colors.value("border").toString()));
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
            palette.setColor(role, QColor(colors.value("text").toString()));
            palette.setColor(QPalette::Disabled, role, QColor(colors.value("muted").toString()));
        }
        palette.setColor(QPalette::Highlight, QColor(colors.value("accent").toString()));
        palette.setColor(QPalette::HighlightedText, QColor(colors.value("accentText").toString()));
        application.setPalette(palette);
    };
    applyWidgetTheme();
    QObject::connect(&uiSettings, &nookbit::UiSettingsController::darkModeChanged, &application, applyWidgetTheme);
    if (organizer) {
        engine.addImageProvider(QStringLiteral("fileicons"), new nookbit::FileIconProvider);
        engine.addImageProvider(QStringLiteral("filepreviews"), new nookbit::FilePreviewProvider);
    }
    // 실제 기능 창구만 빌려주고 꺼진 기능의 대체 컨트롤러나 실행기는 만들지 않는다.
    const auto expose = [&engine](const QString& name, QObject* value) { engine.rootContext()->setContextProperty(name, value); };
    expose("pendingFiles", organizer ? &organizer->pendingFiles() : nullptr);
    expose("backgroundTasks", organizer ? &organizer->tasks() : nullptr);
    expose("previewController", organizer ? &organizer->preview() : nullptr);
    expose("recoveryController", organizer ? &organizer->recovery() : nullptr);
    expose("historyController", organizer ? &organizer->history() : nullptr);
    expose("ruleEditor", organizer ? &organizer->rules() : nullptr);
    expose("watchController", folderWatch ? &folderWatch->controller() : nullptr);
    expose("notifications", notificationRuntime ? &notificationRuntime->controller() : nullptr);
    expose("appLifecycle", &appLifecycle);
    expose("uiSettings", &uiSettings);
    expose("tutorial", &tutorial);
    expose("featureSettings", &featureSettings);
    expose("userGuides", &userGuides);
    expose("uiPages", &uiPages);
    engine.rootContext()->setContextProperty("featureStartupMessages", startup.messages);
    engine.rootContext()->setContextProperty("organizerSettings", organizer ? organizer->settingsUi() : QVariantMap{});
    engine.rootContext()->setContextProperty("watchSettingsEntry", folderWatch ? folderWatch->settingsEntryUi() : QVariantMap{});
    engine.rootContext()->setContextProperty("watchProposalUi", folderWatch ? folderWatch->proposalUi() : QVariantMap{});
    engine.rootContext()->setContextProperty("companionDesktop", companion ? companion->desktopUi() : QVariantMap{});
    if (verifyFeatures) QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&featureBindingsClean](const QList<QQmlError>& errors) {
        // 일부 기능이 없는 상태에서 숨은 필수 참조나 잘못된 바인딩을 놓치지 않는 함수.
        featureBindingsClean = false;
        for (const auto& error : errors) QTextStream(stderr) << error.toString() << Qt::endl;
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 호스트는 공통 트레이 동작을 만들고 컴패니언의 메뉴 기여 수명만 보관한다.
    QMenu trayMenu;
    auto* openAction = trayMenu.addAction(QStringLiteral("NookBit 열기"));
    auto companionTray = companion ? companion->createTrayAction(trayMenu) : std::unique_ptr<nookbit::CompanionTrayAction>{};
    trayMenu.addSeparator();
    auto* exitAction = trayMenu.addAction(QStringLiteral("완전 종료"));
    QSystemTrayIcon tray(application.windowIcon());
    tray.setToolTip(QStringLiteral("NookBit · 창을 닫아도 실행 중"));
    tray.setContextMenu(&trayMenu);
    QObject::connect(openAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::openWindow);
    QObject::connect(exitAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::requestExit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &appLifecycle,
        [&appLifecycle](QSystemTrayIcon::ActivationReason reason) {
            // 왼쪽 클릭 또는 더블클릭으로 숨긴 메인 창을 복원하는 함수.
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) appLifecycle.openWindow();
        });
    if (!engine.rootObjects().isEmpty()) {
        auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QObject::connect(&appLifecycle, &nookbit::AppLifecycleController::hideRequested, mainWindow, &QWindow::hide);
        QObject::connect(&appLifecycle, &nookbit::AppLifecycleController::showRequested, mainWindow, [mainWindow] {
            // 창을 다시 표시하고 키보드 입력을 받을 수 있도록 활성화하는 함수.
            mainWindow->showNormal(); mainWindow->raise(); mainWindow->requestActivate();
        });
    }
    QObject::connect(&appLifecycle, &nookbit::AppLifecycleController::exitRequested, &application,
        [&engine, &uiPreferences] {
            // 마지막 캐릭터 위치를 확정한 후 설정을 반영하고 완전히 종료하는 함수.
            if (!engine.rootObjects().isEmpty()) QMetaObject::invokeMethod(engine.rootObjects().first(), "commitCharacterPosition");
            uiPreferences.sync();
            QCoreApplication::quit();
        });
    // 종료는 막되 창 숨기기는 허용하도록 모든 백그라운드 작업 상태를 합치는 함수.
    const auto refreshExitState = [&appLifecycle, &features, exitAction] {
        const bool busy = features.busy();
        appLifecycle.setBusy(busy);
        exitAction->setEnabled(!busy);
        exitAction->setText(busy ? QStringLiteral("작업 중 · 완료 후 종료 가능") : QStringLiteral("완전 종료"));
    };
    QObject::connect(&features, &nookbit::FeatureRuntimeHost::busyChanged, &appLifecycle, refreshExitState);
    refreshExitState();
    // 시스템 트레이가 없어졌을 때 숨긴 앱이 접근 불가능해지지 않도록 확인하는 함수.
    const auto refreshTray = [&appLifecycle, &tray, verifyUi] {
        const bool available = !verifyUi && QSystemTrayIcon::isSystemTrayAvailable();
        appLifecycle.setTrayAvailable(available);
        if (available && !tray.isVisible()) tray.show();
        else if (!available && tray.isVisible()) tray.hide();
    };
    refreshTray();
    QTimer trayAvailability;
    trayAvailability.setInterval(15000);
    QObject::connect(&trayAvailability, &QTimer::timeout, &appLifecycle, refreshTray);
    if (!verifyUi) trayAvailability.start();

    if (verifyTutorial)
        return nookbit::ui_smoke::verifyTutorial(engine, features, uiPages, uiPreferences, tutorial,
            verifyTutorialSkip, featureBindingsClean);
    if (verifyFeatures)
        return nookbit::ui_smoke::verifyFeatureStartup(engine, features, uiPages, startup, tutorial, trayMenu,
            featureSettings, featurePreferences, savedFeatures, verifyFeaturePreferences, featureBindingsClean);
    if (verifyUi)
        return nookbit::ui_smoke::verifyUi(application, engine, features, uiMetrics, appLifecycle, userGuides);

    return application.exec();
}
