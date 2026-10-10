#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/filesystem/file_operation_service.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/windows_transfer_recovery.hpp"
#include "infrastructure/filesystem/interrupted_operation_recovery_service.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/configuration/runtime_settings_repository.hpp"
#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/test_rule_use_case.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QApplication>
#include <QQuickStyle>
#include <QPalette>
#include "presentation/ui_settings_controller.hpp"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include "presentation/app_lifecycle_controller.hpp"
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QSettings>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include "app/thread_local_operations.hpp"
#include "presentation/background_task_runner.hpp"
#include "presentation/character_controller.hpp"
#include "presentation/character_settings_controller.hpp"
#include "presentation/character_window_controller.hpp"
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QJSValue>
#include "infrastructure/configuration/json_character_presentation.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "infrastructure/configuration/directory_image_pack_library.hpp"

// 내장 SQL로 임시 메모리 DB를 최신 스키마까지 만들 수 있는지 검사하는 함수.
static bool verifyBundledHistorySchema(const QString& migrationPath) {
    const auto connectionName = QStringLiteral("nookbit-bundled-schema-check");
    bool ready = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(QStringLiteral(":memory:"));
        if (database.open()) {
            const auto migrated = nookbit::migrateHistorySchema(database, migrationPath);
            if (migrated) {
                QSqlQuery proofTable(database);
                ready = proofTable.exec(QStringLiteral("SELECT count(*) FROM transfer_removal_proofs")) &&
                        proofTable.next() && proofTable.value(0).toInt() == 0;
            } else {
                QTextStream(stderr) << QString::fromStdString(migrated.error().message) << Qt::endl;
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ready;
}

// 추가 이미지 팩에 의존하지 않는 작은 앱 식별 아이콘을 만드는 함수.
static QIcon applicationIcon() {
    QPixmap image(32, 32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#263246")));
    painter.drawRoundedRect(QRectF(1, 1, 30, 30), 8, 8);
    QFont font; font.setPixelSize(22); font.setBold(true);
    painter.setFont(font); painter.setPen(QColor(QStringLiteral("#b6d8c5")));
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("N"));
    painter.end();
    return QIcon(image);
}

// 실제 확장 창에 마우스 이벤트를 보내 모든 페이지의 접기 버튼 접근성을 검사하는 함수.
static bool verifyExtensionInteraction(QQuickWindow* mainWindow, QQuickWindow* extension, QObject* metrics) {
    auto* button = mainWindow->findChild<QQuickItem*>(QStringLiteral("extensionCloseButton"));
    auto* rules = mainWindow->findChild<QQuickItem*>(QStringLiteral("ruleEditorPage"));
    if (!button || !rules) return false;
    // 창 안의 실제 버튼 좌표로 눌렀다 떼는 입력을 전달하는 함수.
    const auto click = [extension](QQuickItem* item) {
        const auto local = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const QPointF global = extension->mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(extension, &press);
        QCoreApplication::sendEvent(extension, &release);
        QCoreApplication::processEvents();
    };
    for (const int page : {1, 2, 3, 4, 5, 6, 7, 8, 9}) {
        mainWindow->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        if (page == 1) {
            auto* ruleTestButton = rules->findChild<QQuickItem*>(QStringLiteral("ruleTestButton"));
            auto* testPanel = rules->findChild<QObject*>(QStringLiteral("ruleTestPanel"));
            if (!rules->isVisible() || rules->width() <= 0 || rules->height() <= 0 || !ruleTestButton || !testPanel) {
                QTextStream(stderr) << "Rule editor is hidden or covered in the extension window." << Qt::endl;
                return false;
            }
            click(ruleTestButton);
            if (!testPanel->property("opened").toBool()) {
                QTextStream(stderr) << "Rule editor toolbar click is blocked." << Qt::endl;
                return false;
            }
            QMetaObject::invokeMethod(testPanel, "close");
            // 새 크기 블록의 선택 목록이 초안을 직접 갱신하는지 검사한다.
            if (!QMetaObject::invokeMethod(rules, "addBlock", Q_ARG(QVariant, QVariant("size")),
                    Q_ARG(QVariant, QVariant(350)), Q_ARG(QVariant, QVariant(220)))) return false;
            QCoreApplication::processEvents();
            auto* comparison = rules->findChild<QObject*>(QStringLiteral("sizeComparisonCombo"));
            auto* unit = rules->findChild<QObject*>(QStringLiteral("sizeUnitCombo"));
            if (!comparison || !unit || unit->property("count").toInt() != 3) return false;
            // 공통 드롭다운의 높이·화살표 규격과 실제 메뉴 열림을 검사한다.
            for (auto* combo : {comparison, unit}) {
                auto* indicator = combo->property("indicator").value<QObject*>();
                if (combo->property("height").toInt() != metrics->property("controlHeight").toInt() || !indicator
                    || indicator->objectName() != QStringLiteral("appDropdownIndicator")
                    || indicator->property("width").toInt() != metrics->property("dropdownIconWidth").toInt()
                    || indicator->property("height").toInt() != metrics->property("dropdownIconHeight").toInt()) {
                    QTextStream(stderr) << "Dropdown dimensions failed: " << combo->objectName()
                        << " height=" << combo->property("height").toInt() << Qt::endl;
                    return false;
                }
                auto* popup = combo->property("popup").value<QObject*>();
                click(qobject_cast<QQuickItem*>(combo));
                if (!popup || !popup->property("visible").toBool()) {
                    QTextStream(stderr) << "Dropdown popup did not open: " << combo->objectName() << Qt::endl;
                    return false;
                }
                QMetaObject::invokeMethod(popup, "close");
                QCoreApplication::processEvents();
            }
            comparison->setProperty("currentIndex", 1);
            QMetaObject::invokeMethod(comparison, "activated", Q_ARG(int, 1));
            unit->setProperty("currentIndex", 2);
            QMetaObject::invokeMethod(unit, "activated", Q_ARG(int, 2));
            const auto selected = rules->property("selectedBlock").value<QJSValue>();
            if (selected.property("comparison").toString() != "atLeast" || selected.property("unit").toString() != "GB") return false;
        }
        if (page == 8) {
            auto* themeSwitch = mainWindow->findChild<QQuickItem*>(QStringLiteral("globalDarkModeSwitch"));
            const bool initial = mainWindow->property("editorDark").toBool();
            if (!themeSwitch) return false;
            click(themeSwitch);
            if (mainWindow->property("editorDark").toBool() == initial || rules->property("darkMode").toBool() == initial) return false;
            click(themeSwitch);
            if (mainWindow->property("editorDark").toBool() != initial) return false;
        }
        if (page == 3) {
            // 감시 조건 네 항목에 실제 키 입력 후 Enter로 숫자를 확정하는 검사.
            for (const auto* name : {"watchIntervalSpin", "watchCountSpin", "watchSizeSpin", "watchAgeSpin"}) {
                auto* spin = mainWindow->findChild<QQuickItem*>(QString::fromLatin1(name));
                if (!spin || !spin->property("editable").toBool()
                    || spin->height() != metrics->property("controlHeight").toInt()) return false;
                const bool wasEnabled = spin->isEnabled();
                spin->setEnabled(true);
                auto* editor = qobject_cast<QQuickItem*>(spin->property("contentItem").value<QObject*>());
                if (!editor || editor->property("readOnly").toBool()) return false;
                editor->forceActiveFocus();
                extension->requestActivate();
                QCoreApplication::processEvents();
                QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, QStringLiteral("a"));
                QKeyEvent three(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier, QStringLiteral("3"));
                QKeyEvent seven(QEvent::KeyPress, Qt::Key_7, Qt::NoModifier, QStringLiteral("7"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                for (auto* event : {&selectAll, &three, &seven, &enter}) QCoreApplication::sendEvent(extension, event);
                QKeyEvent enterRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(extension, &enterRelease);
                QCoreApplication::processEvents();
                if (spin->property("value").toInt() != 37) {
                    QTextStream(stderr) << "Keyboard number entry failed: " << name << " text=" << editor->property("text").toString()
                        << " value=" << spin->property("value").toInt() << Qt::endl;
                    return false;
                }
                spin->setEnabled(wasEnabled);
            }
        }
        click(button);
        if (mainWindow->property("currentPage").toInt() != 0 || extension->isVisible()) {
            QTextStream(stderr) << "Extension close click blocked on page " << page << Qt::endl;
            return false;
        }
    }
    return true;
}

// 이전 중단 작업을 확인하고 대기 목록·미리보기 화면을 시작하는 함수.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setOrganizationName(QStringLiteral("NookBitProject"));
    QCoreApplication::setApplicationName(QStringLiteral("NookBit"));
    const bool verifyUi = QCoreApplication::arguments().contains(QStringLiteral("--verify-ui"));
    application.setWindowIcon(applicationIcon());
    // 숨긴 메인 창과 도구 창의 개수로 프로세스 수명을 결정하지 않는다.
    application.setQuitOnLastWindowClosed(false);
    nookbit::AppLifecycleController appLifecycle;

    nookbit::PendingFilesModel pendingFiles;
    const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto fallbackDestination = QDir(documents).filePath(QStringLiteral("NookBit Organized"));
    QSettings uiPreferences;
    nookbit::UiSettingsController uiSettings(uiPreferences, !verifyUi);
    // 네이티브 트레이 메뉴도 앱의 밝기 선택을 따르도록 기본 팔레트를 갱신하는 함수.
    const auto lightWidgetPalette = application.palette();
    const auto applyWidgetTheme = [&application, &uiSettings, lightWidgetPalette] {
        QPalette palette = lightWidgetPalette;
        if (uiSettings.darkMode()) {
            palette.setColor(QPalette::Window, QColor("#161a22"));
            palette.setColor(QPalette::Base, QColor("#202633"));
            palette.setColor(QPalette::AlternateBase, QColor("#242d3b"));
            palette.setColor(QPalette::Button, QColor("#2b3546"));
            for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
                palette.setColor(role, QColor("#edf2f8"));
                palette.setColor(QPalette::Disabled, role, QColor("#aab6c7"));
            }
            palette.setColor(QPalette::Highlight, QColor("#405e80"));
            palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        application.setPalette(palette);
    };
    QObject::connect(&uiSettings, &nookbit::UiSettingsController::darkModeChanged, &application, applyWidgetTheme);
    applyWidgetTheme();
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
    nookbit::JsonPendingFilesRepository pendingStorage(
        QDir(appData).filePath(QStringLiteral("pending-files.json")));
    const nookbit::IPendingFilesRepository& pendingRepository = pendingStorage;
    if (!verifyUi) {
        const auto restored = pendingRepository.load();
        if (restored) {
            QStringList paths;
            for (const auto& path : restored.value()) paths.append(QString::fromStdWString(path.wstring()));
            pendingFiles.restorePaths(paths);
            // 등록·제외·이동 완료로 목록이 바뀔 때 경로만 저장하는 함수.
            QObject::connect(&pendingFiles, &nookbit::PendingFilesModel::countChanged,
                             &application, [&pendingFiles, &pendingRepository] {
                std::vector<std::filesystem::path> paths;
                for (const auto& path : pendingFiles.filePaths()) paths.emplace_back(path.toStdWString());
                const auto saved = pendingRepository.save(paths);
                pendingFiles.setStorageNotice(saved ? QString() :
                    QStringLiteral("대기 목록을 저장하지 못했어요. 이번 변경은 재시작 후 복원되지 않을 수 있어요: %1")
                        .arg(QString::fromStdString(saved.error().message)));
            });
        } else {
            pendingFiles.setStorageNotice(QStringLiteral("대기 목록을 복원하지 못했어요. 기존 저장 파일은 보존하며 이번 실행의 목록 변경은 저장하지 않아요: %1")
                .arg(QString::fromStdString(restored.error().message)));
        }
    }
    QTimer pendingAvailabilityTimer;
    pendingAvailabilityTimer.setInterval(3000);
    QObject::connect(&pendingAvailabilityTimer, &QTimer::timeout,
                     &pendingFiles, &nookbit::PendingFilesModel::refreshAvailability);
    if (!verifyUi) pendingAvailabilityTimer.start();
    const auto databasePath = QDir(appData).filePath(QStringLiteral("history.sqlite"));
    const auto migrationPath = QStringLiteral(":/nookbit/001_initial.sql");
    nookbit::SqliteTransferJournal transferJournal(databasePath, migrationPath);
    const auto treeSchema = QStringLiteral(":/nookbit/rules-tree-v3.schema.json");
    const auto legacyTreeSchema = QStringLiteral(":/nookbit/rules-tree.schema.json");
    const auto bundledTree = QStringLiteral(":/nookbit/default-v3.json");
    const auto rulesPath = verifyUi ? bundledTree
        : QDir(appData).filePath(QStringLiteral("rules.json"));
    nookbit::JsonRuleSetRepository previewRules(rulesPath, treeSchema, legacyTreeSchema);
    // 사용자 규칙이 아직 없다면 이전 기본 분류와 같은 v3 규칙을 처음 한 번 저장한다.
    if (!verifyUi && !QFileInfo::exists(rulesPath)) {
        nookbit::JsonRuleSetRepository defaults(bundledTree, treeSchema);
        const auto initial = defaults.load();
        if (initial) previewRules.save(initial.value());
    }
    nookbit::FilesystemScanner previewScanner;
    nookbit::RuleEngine previewRuleEngine;
    nookbit::TestRuleUseCase testRule(previewScanner, previewRuleEngine);
    nookbit::RuleEditorController ruleEditor(
        previewRules, previewRules, nullptr, &previewRules,
        verifyUi ? QString() : QDir(appData).filePath(QStringLiteral("rule-graph-layout.json")),
        &previewRules, &testRule, &previewRules);
    nookbit::WindowsStagedCopy transferCopy;
    nookbit::Planner previewPlanner(previewRuleEngine, std::filesystem::path(appData.toStdWString()), nookbit::CrossVolumeDrafts::Allow);
    nookbit::QtPlanHasher previewHasher;
    nookbit::CreatePreviewUseCase createPreview(
        previewRules, previewScanner, previewPlanner, &previewHasher, &transferCopy);
    nookbit::SqliteHistoryRepository history(databasePath, migrationPath);
    nookbit::FreezePreviewUseCase freezePreview(
        previewRules, previewScanner, previewPlanner, previewHasher, history, &transferCopy);
    nookbit::VerifySavedPlanUseCase verifySavedPlan(
        previewRules, previewScanner, previewPlanner, previewHasher, history, &transferCopy);
    nookbit::ApprovePlanUseCase approvePlan(verifySavedPlan, history);
    nookbit::UserSettings currentSettings;
    currentSettings.homeRoot = std::filesystem::path(defaultDestination.toStdWString());
    nookbit::RuntimeSettingsRepository recoverySettings(currentSettings);
    nookbit::ThreadLocalOperations fileOperations(databasePath, migrationPath, rulesPath, treeSchema, legacyTreeSchema,
        std::filesystem::path(appData.toStdWString()), recoverySettings);
    nookbit::BackgroundTaskRunner backgroundTasks;
    nookbit::PreviewController previewController(
        pendingFiles,
        createPreview,
        freezePreview,
        verifySavedPlan,
        approvePlan,
        history,
        fileOperations,
        rulesPath,
        treeSchema,
        defaultDestination,
        nullptr, legacyTreeSchema);
    if (!verifyUi) {
        previewController.setCollisionPolicy(uiPreferences.value(
            QStringLiteral("collisionPolicy"), QStringLiteral("rename")).toString());
    }
    QObject::connect(&previewController, &nookbit::PreviewController::collisionPolicyChanged,
                     &application, [&uiPreferences, &previewController, verifyUi] {
        if (verifyUi) return;
        // 선택한 공통 충돌 정책을 사용자 설정에 저장하는 함수.
        uiPreferences.setValue(QStringLiteral("collisionPolicy"), previewController.collisionPolicy());
        uiPreferences.sync();
    });
    QObject::connect(&previewController, &nookbit::PreviewController::destinationRootAccepted,
                     &application, [&uiPreferences, verifyUi](const QString& root) {
        if (verifyUi) return;
        // 성공한 미리보기의 목적지만 다음 실행의 기본 경로로 기억하는 함수.
        uiPreferences.setValue(QStringLiteral("destinationRoot"), root);
        uiPreferences.sync();
    });
    // 화면의 공통 충돌 정책을 코어의 현재 설정 값으로 변환하는 함수.
    const auto currentPolicy = [&previewController] {
        const auto key = previewController.collisionPolicy();
        return key == "abort" ? nookbit::CollisionPolicy::Abort : key == "skip" ? nookbit::CollisionPolicy::Skip : nookbit::CollisionPolicy::Rename;
    };
    recoverySettings.setCollisionPolicy(currentPolicy());
    QObject::connect(&previewController, &nookbit::PreviewController::destinationRootAccepted, &application,
        [&recoverySettings](const QString& root) { recoverySettings.setHomeRoot(std::filesystem::path(root.toStdWString())); });
    QObject::connect(&previewController, &nookbit::PreviewController::collisionPolicyChanged, &application,
        [&recoverySettings, &currentPolicy] { recoverySettings.setCollisionPolicy(currentPolicy()); });
    nookbit::RecoveryController recoveryController(history, nullptr, &transferJournal, &fileOperations);
    nookbit::HistoryController historyController(history, fileOperations);
    previewController.setTaskRunner(&backgroundTasks);
    previewController.setSortWorkflow(&fileOperations);
    historyController.setTaskRunner(&backgroundTasks);
    recoveryController.setTaskRunner(&backgroundTasks);
    QObject::connect(&historyController, &nookbit::HistoryController::rollbackAttempted,
                     &previewController, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    QObject::connect(&historyController, &nookbit::HistoryController::recoveryRefreshRequested,
                     &recoveryController, &nookbit::RecoveryController::refreshRecoveryCases);
    QObject::connect(&previewController, &nookbit::PreviewController::recoveryReconcileRequested,
                     &recoveryController, &nookbit::RecoveryController::reconcileAndRefreshCases);
    QObject::connect(&ruleEditor, &nookbit::RuleEditorController::rulesSaved,
                     &previewController, &nookbit::PreviewController::refreshActiveRules);
    QObject::connect(&recoveryController, &nookbit::RecoveryController::recoveryAttempted, &historyController,
        [&historyController, &pendingFiles](const QString&) { historyController.refreshHistory(); pendingFiles.refreshAvailability(); });
    QObject::connect(&recoveryController, &nookbit::RecoveryController::recoveryCompleted, &application,
        [&history, &pendingFiles, &previewController](const QString& id, const QString& action) {
            if (action == "rollback") { previewController.refreshLastUndoAfterHistoryRollback(id); return; }
            const auto plan = history.loadPlan(id.toStdString());
            if (!plan || plan.value().status != nookbit::PlanStatus::Completed) return;
            for (int i = pendingFiles.count() - 1; i >= 0; --i) {
                const std::filesystem::path path(pendingFiles.filePaths()[i].toStdWString());
                if (std::any_of(plan.value().steps.begin(), plan.value().steps.end(), [&](const nookbit::MoveStep& step) {
                    return step.status == nookbit::StepStatus::Applied && step.source == path;
                })) pendingFiles.removeAt(i);
            }
        });
    if (!verifyUi && QFileInfo::exists(databasePath)) {
        recoveryController.refreshRecoveryCases();
    }
    nookbit::JsonWatchSettingsRepository watchSettings(
        QDir(appData).filePath(QStringLiteral("watch.json")),
        QStringLiteral(":/nookbit/watch.schema.json"));
    nookbit::FilesystemScanner watchScanner;
    nookbit::RuleEngine watchRuleEngine;
    nookbit::EvaluateWatchFolderUseCase evaluateWatchFolder(
        watchScanner, watchRuleEngine, previewRules);
    nookbit::WatchController watchController(
        pendingFiles, watchSettings, evaluateWatchFolder,
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    nookbit::CharacterController characterController;
    const auto bundledCharacter = QStringLiteral(":/nookbit/character.presentation.json");
    const auto characterSchema = QStringLiteral(":/nookbit/character-presentation.schema.json");
    const auto customCharacter = QDir(appData).filePath(QStringLiteral("character.presentation.json"));
    auto characterPath = !verifyUi && QFileInfo::exists(customCharacter) ? customCharacter : bundledCharacter;
    auto characterPresentation = nookbit::loadCharacterPresentation(characterPath, characterSchema);
    if (!characterPresentation && !verifyUi) {
        QTextStream(stderr) << QString::fromStdString(characterPresentation.error().message) << Qt::endl;
        characterPath = bundledCharacter;
        characterPresentation = nookbit::loadCharacterPresentation(bundledCharacter, characterSchema);
    }
    if (!characterPresentation) return EXIT_FAILURE;
    const auto characterImageBase = characterPath.startsWith(QStringLiteral(":/"))
        ? QUrl(QStringLiteral("qrc") + QFileInfo(characterPath).path() + QStringLiteral("/"))
        : QUrl::fromLocalFile(QFileInfo(characterPath).absolutePath() + QStringLiteral("/"));
    const auto configuredCharacter = characterController.setPresentation(
        std::move(characterPresentation.value()), characterImageBase);
    if (!configuredCharacter) return EXIT_FAILURE;
    const nookbit::JsonImagePackLoader imagePackLoader;
    nookbit::DirectoryImagePackLibrary imagePackLibrary(imagePackLoader,
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("imagepacks/test/imagepack.json")),
        QDir(verifyUi ? QCoreApplication::applicationDirPath() : appData).filePath(
            verifyUi ? QStringLiteral("verify-unused-imagepacks") : QStringLiteral("imagepacks")));
    // 임포트 수명은 보관소보다 짧게 유지하고 파일 정리 실행기와 분리한다.
    nookbit::BackgroundTaskRunner characterImports;
    nookbit::CharacterSettingsController characterSettings(imagePackLibrary,
        [&characterController](nookbit::ImagePack pack) {
            // 값으로 전달된 팩을 캐릭터 표현 창구에 적용하는 함수.
            return characterController.setImagePack(std::move(pack));
        }, characterImports);
    characterSettings.refresh();
    const auto savedPack = verifyUi ? QString() : uiPreferences.value(QStringLiteral("character/selectedPack")).toString();
    if (!savedPack.isEmpty() && !characterSettings.selectPack(savedPack))
        QTextStream(stderr) << characterSettings.status() << Qt::endl;
    if (characterSettings.selectedKey().isEmpty() && (verifyUi || characterPath == bundledCharacter)) {
        if (!characterSettings.selectPack(QStringLiteral("builtin")) && verifyUi) return EXIT_FAILURE;
    }
    if (!verifyUi) characterSettings.setUserScale(uiPreferences.value(
        QStringLiteral("character/scales/") + characterSettings.selectedKey(), 0.5).toDouble());
    // 선택한 팩과 개인 배율을 사용자 설정에서 복원하는 함수.
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::selected, &application,
        [&uiPreferences, &characterSettings, verifyUi](const QString& key) {
            if (verifyUi) return;
            characterSettings.setUserScale(uiPreferences.value(QStringLiteral("character/scales/") + key, 0.5).toDouble());
            uiPreferences.setValue(QStringLiteral("character/selectedPack"), key);
            uiPreferences.sync();
        });
    // 개인 배율 변경만 저장하고 팩 매니페스트는 수정하지 않는 함수.
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::scaleChanged, &application,
        [&uiPreferences, &characterSettings, verifyUi] {
            if (verifyUi || characterSettings.selectedKey().isEmpty()) return;
            uiPreferences.setValue(QStringLiteral("character/scales/") + characterSettings.selectedKey(), characterSettings.userScale());
            uiPreferences.sync();
        });
    // 화면의 작업 표시줄을 제외한 사용 가능 영역을 수집하는 함수.
    const auto characterScreens = [] {
        QList<QRect> areas;
        for (const auto* screen : QGuiApplication::screens()) areas.append(screen->availableGeometry());
        return areas;
    };
    const auto screens = characterScreens();
    const auto initialAnchor = screens.isEmpty() ? QPoint(200, 200)
        : QPoint(screens.first().right() - 180, screens.first().bottom() - 140);
    nookbit::CharacterWindowController characterWindow(screens,
        verifyUi ? initialAnchor : uiPreferences.value(QStringLiteral("character/windowAnchor"), initialAnchor).toPoint(),
        verifyUi || uiPreferences.value(QStringLiteral("character/windowShown"), true).toBool());
    // 독립 창 위치와 노출만 사용자 설정에 저장하는 함수.
    const auto saveCharacterWindow = [&uiPreferences, &characterWindow, verifyUi] {
        if (verifyUi) return;
        uiPreferences.setValue(QStringLiteral("character/windowAnchor"), QPoint(characterWindow.anchorX(), characterWindow.anchorY()));
        uiPreferences.setValue(QStringLiteral("character/windowShown"), characterWindow.shown());
        uiPreferences.sync();
    };
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::positionChanged, &application, saveCharacterWindow);
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::visibilityChanged, &application, saveCharacterWindow);
    // 모니터 추가·제거와 작업 영역 변경 시 보이지 않는 위치를 복원하는 함수.
    const auto refreshCharacterScreens = [&characterWindow, characterScreens] { characterWindow.setScreens(characterScreens()); };
    QObject::connect(&application, &QGuiApplication::screenRemoved, &characterWindow, refreshCharacterScreens);
    QObject::connect(&application, &QGuiApplication::screenAdded, &characterWindow,
        [&characterWindow, refreshCharacterScreens](QScreen* screen) {
            // 새 화면의 작업 영역 변경도 같은 위치 복원 창구에 연결하는 함수.
            QObject::connect(screen, &QScreen::availableGeometryChanged, &characterWindow, refreshCharacterScreens);
            refreshCharacterScreens();
        });
    for (auto* screen : QGuiApplication::screens())
        QObject::connect(screen, &QScreen::availableGeometryChanged, &characterWindow, refreshCharacterScreens);
    nookbit::ICharacterChannel& characterChannel = characterController;
    // 파일 작업 상태를 공통 입력으로 변환해 인터페이스 창구에 전달하는 함수.
    const auto reportCharacterActivity = [&characterChannel, &backgroundTasks] {
        const auto received = characterChannel.publish({"file-operations", "current-operation",
            nookbit::CharacterUpdateKind::Activity, backgroundTasks.fileWorkActive(), {}, "file.work"});
        if (!received) QTextStream(stderr) << QString::fromStdString(received.error().message) << Qt::endl;
    };
    // 감시 제안 상태를 공통 주의 요청으로 변환해 인터페이스 창구에 전달하는 함수.
    const auto reportCharacterAttention = [&characterChannel, &watchController] {
        const auto received = characterChannel.publish({"folder-watch", "organization-offer",
            nookbit::CharacterUpdateKind::Attention, watchController.proposalVisible(), {}, "file.offer"});
        if (!received) QTextStream(stderr) << QString::fromStdString(received.error().message) << Qt::endl;
    };
    // 작업 상태를 값으로 전달하고 캐릭터가 다른 컨트롤러를 호출하지 않게 연결하는 함수.
    QObject::connect(&backgroundTasks, &nookbit::BackgroundTaskRunner::busyChanged,
        &characterController, reportCharacterActivity);
    // 감시 제안의 생성·수락·나중에 처리 결과를 캐릭터에 전달하는 함수.
    QObject::connect(&watchController, &nookbit::WatchController::watchChanged,
        &characterController, reportCharacterAttention);
    reportCharacterActivity();
    reportCharacterAttention();
    // 제안 대기 시각을 같은 감시 폴더에 한해 사용자 설정에 저장하는 함수.
    QObject::connect(&watchController, &nookbit::WatchController::snoozeDeadlineChanged,
        &application, [&uiPreferences, &watchController, verifyUi](qint64 deadline) {
            if (verifyUi) return;
            uiPreferences.setValue(QStringLiteral("watchSnoozeFolder"), watchController.folder());
            uiPreferences.setValue(QStringLiteral("watchSnoozeUntilUtcMs"), deadline);
            uiPreferences.sync();
        });
    if (!verifyUi && QString::compare(
            QDir::cleanPath(uiPreferences.value(QStringLiteral("watchSnoozeFolder")).toString()),
            QDir::cleanPath(watchController.folder()), Qt::CaseInsensitive) == 0) {
        watchController.restoreSnoozeUntil(
            uiPreferences.value(QStringLiteral("watchSnoozeUntilUtcMs"), 0).toLongLong());
    }
    QObject::connect(&ruleEditor, &nookbit::RuleEditorController::rulesSaved,
                     &watchController, &nookbit::WatchController::rulesChanged);
    QQmlApplicationEngine engine;
    // QML 공통 규격을 네이티브 메뉴·대화상자의 기본 글씨에도 적용한다.
    auto* uiMetrics = engine.singletonInstance<QObject*>("NookBit", "UiMetrics");
    if (!uiMetrics || uiMetrics->property("textBody").toInt() <= 0) {
        QTextStream(stderr) << "Cannot load shared UI metrics." << Qt::endl;
        return 1;
    }
    QFont baseFont = application.font();
    baseFont.setPixelSize(uiMetrics->property("textBody").toInt());
    application.setFont(baseFont);
    engine.rootContext()->setContextProperty(QStringLiteral("pendingFiles"), &pendingFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("backgroundTasks"), &backgroundTasks);
    engine.rootContext()->setContextProperty(QStringLiteral("previewController"), &previewController);
    engine.rootContext()->setContextProperty(QStringLiteral("recoveryController"), &recoveryController);
    engine.rootContext()->setContextProperty(QStringLiteral("historyController"), &historyController);
    engine.rootContext()->setContextProperty(QStringLiteral("watchController"), &watchController);
    engine.rootContext()->setContextProperty(QStringLiteral("characterController"), &characterController);
    engine.rootContext()->setContextProperty(QStringLiteral("characterSettings"), &characterSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("characterWindow"), &characterWindow);
    engine.rootContext()->setContextProperty(QStringLiteral("appLifecycle"), &appLifecycle);
    engine.rootContext()->setContextProperty(QStringLiteral("uiSettings"), &uiSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("ruleEditor"), &ruleEditor);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 트레이 요청을 화면·캐릭터 상태에 연결하는 일은 조립부에서만 처리한다.
    QMenu trayMenu;
    auto* openAction = trayMenu.addAction(QStringLiteral("NookBit 열기"));
    auto* characterAction = trayMenu.addAction(QStringLiteral("캐릭터 표시"));
    characterAction->setCheckable(true);
    characterAction->setChecked(characterWindow.shown());
    trayMenu.addSeparator();
    auto* exitAction = trayMenu.addAction(QStringLiteral("완전 종료"));
    QSystemTrayIcon tray(application.windowIcon());
    tray.setToolTip(QStringLiteral("NookBit · 창을 닫아도 실행 중"));
    tray.setContextMenu(&trayMenu);
    QObject::connect(openAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::openWindow);
    QObject::connect(exitAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::requestExit);
    QObject::connect(characterAction, &QAction::triggered, &characterWindow, &nookbit::CharacterWindowController::setShown);
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::visibilityChanged, &trayMenu,
        [&characterWindow, characterAction] {
            // 캐릭터 설정과 트레이 메뉴의 표시 상태를 일치시키는 함수.
            characterAction->setChecked(characterWindow.shown());
        });
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
    const auto refreshExitState = [&appLifecycle, &backgroundTasks, &characterImports, exitAction] {
        const bool busy = backgroundTasks.busy() || characterImports.busy();
        appLifecycle.setBusy(busy);
        exitAction->setEnabled(!busy);
        exitAction->setText(busy ? QStringLiteral("작업 중 · 완료 후 종료 가능") : QStringLiteral("완전 종료"));
    };
    QObject::connect(&backgroundTasks, &nookbit::BackgroundTaskRunner::busyChanged, &appLifecycle, refreshExitState);
    QObject::connect(&characterImports, &nookbit::BackgroundTaskRunner::busyChanged, &appLifecycle, refreshExitState);
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

    // 자동 검사에서는 화면과 리소스를 확인하고 이벤트를 한 번 처리한 뒤 종료한다.
    if (verifyUi) {
        if (!verifyBundledHistorySchema(migrationPath)) return EXIT_FAILURE;
        bool compactLayout = false;
        if (!engine.rootObjects().isEmpty()) {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            auto* home = window ? window->findChild<QObject*>(QStringLiteral("compactHome")) : nullptr;
            auto* developer = window ? window->findChild<QObject*>(QStringLiteral("developerTools")) : nullptr;
            auto* extension = window ? window->findChild<QQuickWindow*>(QStringLiteral("extensionWindow")) : nullptr;
            if (window && home && developer && extension) {
                // 다른 창의 배치·노출 문제를 검사 단계 이름으로 식별하는 함수.
                const auto checkLayout = [&compactLayout](bool condition, const char* stage) {
                    if (!condition) { compactLayout = false; QTextStream(stderr) << "Layout check failed: " << stage << Qt::endl; }
                };
                // 본체 크기를 유지하는 별도 창·프레임 제거·연결 위치·개발 도구 숨김을 검사한다.
                compactLayout = true;
                auto* rulePage = window->findChild<QQuickItem*>(QStringLiteral("ruleEditorPage"));
                auto* closeButton = window->findChild<QQuickItem*>(QStringLiteral("extensionCloseButton"));
                checkLayout(rulePage && rulePage->window() == extension, "rule page belongs to extension window");
                checkLayout(closeButton && closeButton->window() == extension, "close button belongs to extension window");
                checkLayout(window->width() == 380 && !developer->property("visible").toBool()
                    && window->flags().testFlag(Qt::FramelessWindowHint) && !extension->isVisible(), "initial");
                QMetaObject::invokeMethod(home, "pageRequested", Q_ARG(int, 1));
                checkLayout(window->width() == 380 && extension->isVisible()
                    && extension->transientParent() == window && extension->flags().testFlag(Qt::FramelessWindowHint), "open extension");
                const auto previousX = window->x();
                window->setX(previousX + 12);
                QCoreApplication::processEvents();
                const auto expectedDock = window->x() + window->width() + 10;
                checkLayout(std::abs(extension->property("dockX").toDouble() - expectedDock) < 1, "follow position");
                // 음수 좌표와 다른 모니터 영역에서도 위치를 되돌려 가두지 않는지 검사한다.
                for (const int position : {-1400, 1500, 3500}) {
                    window->setX(position);
                    QCoreApplication::processEvents();
                    checkLayout(extension->x() == window->x() + window->width() + 10, "cross-monitor follow");
                }
                QMetaObject::invokeMethod(home, "pageRequested", Q_ARG(int, 7));
                QCoreApplication::processEvents();
                auto* filePage = window->findChild<QQuickItem*>(QStringLiteral("fileSortPage"));
                checkLayout(filePage && filePage->isVisible() && filePage->window() == extension, "file sorting is a separate page");
                auto* undoButton = window->findChild<QQuickItem*>(QStringLiteral("compactUndoButton"));
                checkLayout(undoButton && undoButton->width() >= undoButton->implicitWidth() && undoButton->width() >= 60, "undo label width");
                window->setX(previousX);
                QCoreApplication::processEvents();
                window->hide();
                checkLayout(!extension->isVisible(), "hide main");
                window->showNormal();
                checkLayout(extension->isVisible(), "restore main");
                window->setProperty("currentPage", 9);
                checkLayout(developer->property("visible").toBool(), "developer tools");
                window->setProperty("currentPage", 0);
                checkLayout(window->width() == 380 && !extension->isVisible() && !developer->property("visible").toBool(), "close extension");
                checkLayout(verifyExtensionInteraction(window, extension, uiMetrics), "page content and real close clicks");
            }
        }
        bool closeToTray = false;
        if (!engine.rootObjects().isEmpty()) {
            auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (mainWindow) {
                // 실제 시스템 트레이 없이도 QML 닫기와 화면 복원 연결을 검사한다.
                appLifecycle.setTrayAvailable(true);
                const bool intercepted = !mainWindow->close();
                closeToTray = intercepted && !mainWindow->isVisible() && characterWindow.shown();
                appLifecycle.openWindow();
                closeToTray = closeToTray && mainWindow->isVisible();
                appLifecycle.setTrayAvailable(false);
            }
        }
        const nookbit::JsonRuleSetRepository bundledRules(bundledTree, treeSchema);
        QObject* editorPage = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("ruleEditorPage"));
        QObject* ruleTestPanel = editorPage
            ? editorPage->findChild<QObject*>(QStringLiteral("ruleTestPanel")) : nullptr;
        QObject* watchSettingsDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("watchSettingsDialog"));
        QObject* historyDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyDialog"));
        QObject* recoveryDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("recoveryDialog"));
        QObject* recoveryConfirmDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("recoveryConfirmDialog"));
        QObject* characterSettingsDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterSettingsDialog"));
        auto* desktopCharacter = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QQuickWindow*>(QStringLiteral("desktopCharacterWindow"));
        QObject* desktopSprite = desktopCharacter
            ? desktopCharacter->findChild<QObject*>(QStringLiteral("desktopCharacterSprite")) : nullptr;
        auto* characterBubble = desktopCharacter
            ? desktopCharacter->findChild<QQuickWindow*>(QStringLiteral("characterBubbleWindow")) : nullptr;
        const bool ready = compactLayout && closeToTray && !engine.rootObjects().isEmpty() &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("compactHome")) != nullptr &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("extensionPanel")) != nullptr &&
                           characterSettingsDialog != nullptr &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openCharacterSettings") &&
                           QMetaObject::invokeMethod(characterSettingsDialog, "open") &&
                           characterSettingsDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(characterSettingsDialog, "close") &&
                           characterSettings.selectedKey() == QStringLiteral("builtin") &&
                           desktopCharacter != nullptr && desktopSprite != nullptr &&
                           characterBubble != nullptr && characterBubble->color().alpha() == 0 &&
                           QMetaObject::invokeMethod(desktopCharacter, "testBubble") &&
                           desktopCharacter->property("testingBubble").toBool() &&
                           desktopCharacter->color().alpha() == 0 &&
                           desktopCharacter->flags().testFlag(Qt::FramelessWindowHint) &&
                           desktopCharacter->flags().testFlag(Qt::WindowStaysOnTopHint) &&
                           desktopCharacter->transientParent() == nullptr &&
                           !desktopCharacter->close() && !characterWindow.shown() &&
                           editorPage != nullptr && ruleTestPanel != nullptr &&
                           QMetaObject::invokeMethod(ruleTestPanel, "open") &&
                           ruleTestPanel->property("opened").toBool() &&
                           QMetaObject::invokeMethod(ruleTestPanel, "close") &&
                           watchSettingsDialog != nullptr && historyDialog != nullptr && recoveryDialog != nullptr && recoveryConfirmDialog != nullptr &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openWatchSettings") &&
                           watchSettingsDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(watchSettingsDialog, "close") &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openHistory") &&
                           historyDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(historyDialog, "close") &&
                           QMetaObject::invokeMethod(recoveryDialog, "open") && recoveryDialog->property("visible").toBool() &&
                           QMetaObject::invokeMethod(recoveryDialog, "close") &&
                           QMetaObject::invokeMethod(recoveryConfirmDialog, "open") && recoveryConfirmDialog->property("visible").toBool() &&
                           QMetaObject::invokeMethod(recoveryConfirmDialog, "close") &&
                           pendingFiles.count() == 0 && !previewController.hasPreview() &&
                           bundledRules.load().hasValue() &&
                           QFile::exists(QStringLiteral(":/nookbit/001_initial.sql")) &&
                           QFile::exists(QStringLiteral(":/nookbit/imagepack.schema.json")) &&
                           QFile::exists(QStringLiteral(":/nookbit/watch.schema.json"));
        if (!ready) {
            return EXIT_FAILURE;
        }
        // 비동기로 읽는 내장 PNG의 크기 검증까지 마친 뒤 자동 검사를 완료한다.
        characterWindow.setShown(true);
        auto* sprite = desktopSprite;
        QTimer spriteCheck;
        spriteCheck.setInterval(10);
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &application, [] { QCoreApplication::exit(EXIT_FAILURE); });
        QObject::connect(&spriteCheck, &QTimer::timeout, &application, [sprite] {
            if (sprite->property("ready").toBool()) QCoreApplication::exit(EXIT_SUCCESS);
            else if (!sprite->property("error").toString().isEmpty()) {
                QTextStream(stderr) << sprite->property("error").toString() << Qt::endl;
                QCoreApplication::exit(EXIT_FAILURE);
            }
        });
        spriteCheck.start();
        deadline.start(2000);
        return application.exec();
    }

    return application.exec();
}
