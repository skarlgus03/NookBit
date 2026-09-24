#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
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
    nookbit::PreviewController previewController(
        pendingFiles,
        QStringLiteral(":/nookbit/rules.json"),
        QStringLiteral(":/nookbit/rules.schema.json"),
        std::filesystem::path(appData.toStdWString()),
        QDir(documents).filePath(QStringLiteral("NookBit Organized")),
        databasePath,
        migrationPath);
    previewController.setRecoveryNotice(recoveryNotice);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pendingFiles"), &pendingFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("previewController"), &previewController);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 자동 검사에서는 화면 객체 생성까지만 확인하고 이벤트 루프에 들어가지 않는다.
    if (verifyUi) {
        const nookbit::JsonRuleSetRepository bundledRules(
            QStringLiteral(":/nookbit/rules.json"),
            QStringLiteral(":/nookbit/rules.schema.json"));
        const bool ready = !engine.rootObjects().isEmpty() && bundledRules.load().hasValue() &&
                           QFile::exists(QStringLiteral(":/nookbit/001_initial.sql"));
        return ready ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    return application.exec();
}
