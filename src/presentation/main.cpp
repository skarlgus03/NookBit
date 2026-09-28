#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
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
    const auto databasePath = QDir(appData).filePath(QStringLiteral("history.sqlite"));
    const auto migrationPath = QStringLiteral(":/nookbit/001_initial.sql");
    QString recoveryNotice;
    if (!verifyUi && QFileInfo::exists(databasePath)) {
        nookbit::SqliteHistoryRepository recoveryHistory(databasePath, migrationPath);
        nookbit::FilesystemScanner recoveryScanner;
        nookbit::QtPlanHasher recoveryHasher;
        nookbit::RecoveryService recovery(recoveryHistory, recoveryScanner, recoveryHasher);
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
    nookbit::JsonRuleSetRepository previewRules(
        QStringLiteral(":/nookbit/rules.json"),
        QStringLiteral(":/nookbit/rules.schema.json"));
    nookbit::FilesystemScanner previewScanner;
    nookbit::RuleEngine previewRuleEngine;
    nookbit::Planner previewPlanner(previewRuleEngine, std::filesystem::path(appData.toStdWString()));
    nookbit::QtPlanHasher previewHasher;
    nookbit::CreatePreviewUseCase createPreview(
        previewRules, previewScanner, previewPlanner, &previewHasher);
    nookbit::SqliteHistoryRepository history(databasePath, migrationPath);
    nookbit::FreezePreviewUseCase freezePreview(
        previewRules, previewScanner, previewPlanner, previewHasher, history);
    nookbit::VerifySavedPlanUseCase verifySavedPlan(
        previewRules, previewScanner, previewPlanner, previewHasher, history);
    nookbit::ApprovePlanUseCase approvePlan(verifySavedPlan, history);
    nookbit::PreviewController previewController(
        pendingFiles,
        createPreview,
        freezePreview,
        verifySavedPlan,
        approvePlan,
        history,
        QStringLiteral(":/nookbit/rules.json"),
        QStringLiteral(":/nookbit/rules.schema.json"),
        std::filesystem::path(appData.toStdWString()),
        QDir(documents).filePath(QStringLiteral("NookBit Organized")));
    nookbit::RecoveryController recoveryController(history);
    nookbit::HistoryController historyController(
        history, std::filesystem::path(appData.toStdWString()),
        QDir(documents).filePath(QStringLiteral("NookBit Organized")));
    QObject::connect(&historyController, &nookbit::HistoryController::rollbackAttempted,
                     &previewController, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    QObject::connect(&historyController, &nookbit::HistoryController::recoveryRefreshRequested,
                     &recoveryController, &nookbit::RecoveryController::refreshRecoveryCases);
    QObject::connect(&previewController, &nookbit::PreviewController::recoveryReconcileRequested,
                     &recoveryController, &nookbit::RecoveryController::reconcileAndRefreshCases);
    recoveryController.setRecoveryNotice(recoveryNotice);
    if (!verifyUi && QFileInfo::exists(databasePath)) {
        recoveryController.refreshRecoveryCases();
    }
    nookbit::JsonWatchSettingsRepository watchSettings(
        QDir(appData).filePath(QStringLiteral("watch.json")),
        QStringLiteral(":/nookbit/watch.schema.json"));
    nookbit::JsonRuleSetRepository watchRules(
        QStringLiteral(":/nookbit/rules.json"),
        QStringLiteral(":/nookbit/rules.schema.json"));
    nookbit::FilesystemScanner watchScanner;
    nookbit::RuleEngine watchRuleEngine;
    nookbit::EvaluateWatchFolderUseCase evaluateWatchFolder(
        watchScanner, watchRuleEngine, watchRules);
    nookbit::WatchController watchController(
        pendingFiles, watchSettings, evaluateWatchFolder,
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pendingFiles"), &pendingFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("previewController"), &previewController);
    engine.rootContext()->setContextProperty(QStringLiteral("recoveryController"), &recoveryController);
    engine.rootContext()->setContextProperty(QStringLiteral("historyController"), &historyController);
    engine.rootContext()->setContextProperty(QStringLiteral("watchController"), &watchController);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 자동 검사에서는 화면과 리소스를 확인하고 이벤트를 한 번 처리한 뒤 종료한다.
    if (verifyUi) {
        const nookbit::JsonRuleSetRepository bundledRules(
            QStringLiteral(":/nookbit/rules.json"),
            QStringLiteral(":/nookbit/rules.schema.json"));
        const bool ready = !engine.rootObjects().isEmpty() && bundledRules.load().hasValue() &&
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
