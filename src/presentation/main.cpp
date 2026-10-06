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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <filesystem>

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
    QString recoveryNotice;
    if (!verifyUi && QFileInfo::exists(databasePath)) {
        nookbit::SqliteHistoryRepository recoveryHistory(databasePath, migrationPath);
        nookbit::FilesystemScanner recoveryScanner;
        nookbit::QtPlanHasher recoveryHasher;
        nookbit::RecoveryService recovery(recoveryHistory, recoveryScanner, recoveryHasher, &transferJournal);
        const auto result = recovery.reconcile();
        if (!result) {
            recoveryNotice = QStringLiteral("이전 작업 상태를 확인하지 못했어요: %1")
                                 .arg(QString::fromStdString(result.error().message));
        } else if (!result.value().needsAttention.empty()) {
            recoveryNotice = QStringLiteral("중단된 작업 %1건은 자동으로 판단할 수 없어 확인이 필요해요. 파일은 건드리지 않았어요.")
                                 .arg(result.value().needsAttention.size());
        } else if (result.value().reconciled > 0) {
            recoveryNotice = QStringLiteral("이전 중단 작업 %1건의 기록을 실제 파일 상태와 맞췄어요. 파일은 이동하지 않았어요.")
                                 .arg(result.value().reconciled);
        }
    }
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
    nookbit::WindowsTransferSourceRemoval transferRemoval;
    nookbit::WindowsTransferFileInspector transferFiles;
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
    nookbit::ExecuteTransferStepUseCase forwardTransfer(transferCopy, transferRemoval, transferJournal, history, previewHasher);
    nookbit::RollbackTransferUseCase reverseTransfer(transferJournal, history, transferCopy, transferRemoval, transferFiles, previewHasher);
    nookbit::FileOperationService fileOperations(verifySavedPlan, previewScanner, previewHasher, history,
        std::filesystem::path(appData.toStdWString()), &transferCopy, &forwardTransfer, &reverseTransfer);
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
    nookbit::RecoveryController recoveryController(history, nullptr, &transferJournal);
    nookbit::HistoryController historyController(history, fileOperations);
    QObject::connect(&historyController, &nookbit::HistoryController::rollbackAttempted,
                     &previewController, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    QObject::connect(&historyController, &nookbit::HistoryController::recoveryRefreshRequested,
                     &recoveryController, &nookbit::RecoveryController::refreshRecoveryCases);
    QObject::connect(&previewController, &nookbit::PreviewController::recoveryReconcileRequested,
                     &recoveryController, &nookbit::RecoveryController::reconcileAndRefreshCases);
    QObject::connect(&ruleEditor, &nookbit::RuleEditorController::rulesSaved,
                     &previewController, &nookbit::PreviewController::refreshActiveRules);
    recoveryController.setRecoveryNotice(recoveryNotice);
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
    QObject::connect(&ruleEditor, &nookbit::RuleEditorController::rulesSaved,
                     &watchController, &nookbit::WatchController::rulesChanged);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pendingFiles"), &pendingFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("previewController"), &previewController);
    engine.rootContext()->setContextProperty(QStringLiteral("recoveryController"), &recoveryController);
    engine.rootContext()->setContextProperty(QStringLiteral("historyController"), &historyController);
    engine.rootContext()->setContextProperty(QStringLiteral("watchController"), &watchController);
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
        const nookbit::JsonRuleSetRepository bundledRules(bundledTree, treeSchema);
        QObject* editorPage = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("ruleEditorPage"));
        QObject* ruleTestPanel = editorPage
            ? editorPage->findChild<QObject*>(QStringLiteral("ruleTestPanel")) : nullptr;
        QObject* watchSettingsDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("watchSettingsDialog"));
        QObject* historyDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyDialog"));
        const bool ready = !engine.rootObjects().isEmpty() &&
                           editorPage != nullptr && ruleTestPanel != nullptr &&
                           QMetaObject::invokeMethod(ruleTestPanel, "open") &&
                           ruleTestPanel->property("opened").toBool() &&
                           QMetaObject::invokeMethod(ruleTestPanel, "close") &&
                           watchSettingsDialog != nullptr && historyDialog != nullptr &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openWatchSettings") &&
                           watchSettingsDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(watchSettingsDialog, "close") &&
                           QMetaObject::invokeMethod(historyDialog, "open") &&
                           historyDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(historyDialog, "close") &&
                           pendingFiles.count() == 0 && !previewController.hasPreview() &&
                           bundledRules.load().hasValue() &&
                           QFile::exists(QStringLiteral(":/nookbit/001_initial.sql")) &&
                           QFile::exists(QStringLiteral(":/nookbit/watch.schema.json"));
        if (!ready) {
            return EXIT_FAILURE;
        }
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
        return application.exec();
    }

    return application.exec();
}
