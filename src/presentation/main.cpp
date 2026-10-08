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
#include "app/thread_local_operations.hpp"
#include "presentation/background_task_runner.hpp"
#include "presentation/character_controller.hpp"
#include "infrastructure/configuration/json_character_presentation.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"

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

// 이전 중단 작업을 확인하고 대기 목록·미리보기 화면을 시작하는 함수.
int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("NookBitProject"));
    QCoreApplication::setApplicationName(QStringLiteral("NookBit"));
    const bool verifyUi = QCoreApplication::arguments().contains(QStringLiteral("--verify-ui"));

    nookbit::PendingFilesModel pendingFiles;
    const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto fallbackDestination = QDir(documents).filePath(QStringLiteral("NookBit Organized"));
    QSettings uiPreferences;
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
    // 기본 테스트 캐릭터는 ImagePack 로더의 공통 프레임으로 재생한다.
    if (verifyUi || characterPath == bundledCharacter) {
        const nookbit::JsonImagePackLoader imagePackLoader;
        const auto packPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("imagepacks/test/imagepack.json"));
        auto imagePack = imagePackLoader.load(std::filesystem::path(packPath.toStdWString()));
        if (!imagePack) {
            QTextStream(stderr) << QString::fromStdString(imagePack.error().message) << Qt::endl;
            if (verifyUi) return EXIT_FAILURE;
        } else {
            const auto applied = characterController.setImagePack(std::move(imagePack.value()));
            if (!applied) return EXIT_FAILURE;
        }
    }
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
    engine.rootContext()->setContextProperty(QStringLiteral("pendingFiles"), &pendingFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("backgroundTasks"), &backgroundTasks);
    engine.rootContext()->setContextProperty(QStringLiteral("previewController"), &previewController);
    engine.rootContext()->setContextProperty(QStringLiteral("recoveryController"), &recoveryController);
    engine.rootContext()->setContextProperty(QStringLiteral("historyController"), &historyController);
    engine.rootContext()->setContextProperty(QStringLiteral("watchController"), &watchController);
    engine.rootContext()->setContextProperty(QStringLiteral("characterController"), &characterController);
    engine.rootContext()->setContextProperty(QStringLiteral("ruleEditor"), &ruleEditor);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 자동 검사에서는 화면과 리소스를 확인하고 이벤트를 한 번 처리한 뒤 종료한다.
    if (verifyUi) {
        if (!verifyBundledHistorySchema(migrationPath)) return EXIT_FAILURE;
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
        const bool ready = !engine.rootObjects().isEmpty() &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterBadge")) != nullptr &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterSprite")) != nullptr &&
                           editorPage != nullptr && ruleTestPanel != nullptr &&
                           QMetaObject::invokeMethod(ruleTestPanel, "open") &&
                           ruleTestPanel->property("opened").toBool() &&
                           QMetaObject::invokeMethod(ruleTestPanel, "close") &&
                           watchSettingsDialog != nullptr && historyDialog != nullptr && recoveryDialog != nullptr && recoveryConfirmDialog != nullptr &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openWatchSettings") &&
                           watchSettingsDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(watchSettingsDialog, "close") &&
                           QMetaObject::invokeMethod(historyDialog, "open") &&
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
        auto* sprite = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterSprite"));
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
