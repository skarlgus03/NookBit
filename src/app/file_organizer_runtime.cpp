#include "app/file_organizer_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include "app/pending_queue_adapter.hpp"
#include "app/thread_local_operations.hpp"
#include "application/use_cases/classify_files_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/test_rule_use_case.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"
#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/destination_settings_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <algorithm>

namespace nookbit {
namespace {
// 첫 실행에만 기본 규칙을 복사하고 기존 사용자 규칙은 보존하는 함수.
QString prepareRules(const QString& appData, bool verifyUi) {
    const auto bundled = QStringLiteral(":/nookbit/default-v3.json");
    const auto path = verifyUi ? bundled : QDir(appData).filePath(QStringLiteral("rules.json"));
    if (!verifyUi && !QFileInfo::exists(path)) {
        JsonRuleSetRepository defaults(bundled, QStringLiteral(":/nookbit/rules-tree-v3.schema.json"));
        const auto initial = defaults.load();
        if (initial) {
            JsonRuleSetRepository target(path, QStringLiteral(":/nookbit/rules-tree-v3.schema.json"));
            (void)target.save(initial.value());
        }
    }
    return path;
}
// 최초 목적지를 코어 설정의 값 스냅샷으로 만드는 함수.
UserSettings initialSettings(const QString& destination) {
    UserSettings settings;
    settings.homeRoot = std::filesystem::path(destination.toStdWString());
    return settings;
}
}

/// 선언 역순으로 실행기부터 해제하여 작업이 사용하는 저장소와 설정을 보존하는 내부 상태.
struct FileOrganizerRuntime::State {
    QSettings& uiPreferences;
    const bool verifyUi;
    const QString appData, defaultDestination, databasePath;
    const QString migrationPath{QStringLiteral(":/nookbit/001_initial.sql")};
    const QString treeSchema{QStringLiteral(":/nookbit/rules-tree-v3.schema.json")};
    const QString legacyTreeSchema{QStringLiteral(":/nookbit/rules-tree.schema.json")};
    const QString rulesPath;
    PendingFilesModel pendingFiles;
    PendingQueueAdapter pendingQueue;
    JsonPendingFilesRepository pendingStorage;
    const IPendingFilesRepository& pendingRepository;
    QTimer pendingAvailabilityTimer;
    SqliteTransferJournal transferJournal;
    JsonRuleSetRepository previewRules;
    FilesystemScanner previewScanner;
    RuleEngine previewRuleEngine;
    TestRuleUseCase testRule;
    RuleEditorController ruleEditor;
    WindowsStagedCopy transferCopy;
    Planner previewPlanner;
    QtPlanHasher previewHasher;
    CreatePreviewUseCase createPreview;
    SqliteHistoryRepository history;
    FreezePreviewUseCase freezePreview;
    VerifySavedPlanUseCase verifySavedPlan;
    ApprovePlanUseCase approvePlan;
    RuntimeSettingsRepository recoverySettings;
    DestinationSettingsController destinationSettings;
    ThreadLocalOperations fileOperations;
    PreviewController previewController;
    RecoveryController recoveryController;
    HistoryController historyController;
    ClassifyFilesUseCase classificationQuery;
    BackgroundTaskRunner backgroundTasks;
    QObject connections;

    // 저장소와 일꾼을 먼저 만들고 내부 입력·설정 연결을 구성하는 생성자.
    State(FileOrganizerRuntimeConfig config, QSettings& preferences)
        : uiPreferences(preferences), verifyUi(config.verifyUi), appData(std::move(config.appData)),
          defaultDestination(std::move(config.defaultDestination)),
          databasePath(QDir(appData).filePath(QStringLiteral("history.sqlite"))),
          rulesPath(prepareRules(appData, verifyUi)), pendingQueue(pendingFiles),
          pendingStorage(QDir(appData).filePath(QStringLiteral("pending-files.json"))), pendingRepository(pendingStorage),
          transferJournal(databasePath, migrationPath), previewRules(rulesPath, treeSchema, legacyTreeSchema),
          testRule(previewScanner, previewRuleEngine),
          ruleEditor(previewRules, previewRules, nullptr, &previewRules,
              verifyUi ? QString() : QDir(appData).filePath(QStringLiteral("rule-graph-layout.json")),
              &previewRules, &testRule, &previewRules),
          previewPlanner(previewRuleEngine, std::filesystem::path(appData.toStdWString()), CrossVolumeDrafts::Allow),
          createPreview(previewRules, previewScanner, previewPlanner, &previewHasher, &transferCopy),
          history(databasePath, migrationPath),
          freezePreview(previewRules, previewScanner, previewPlanner, previewHasher, history, &transferCopy),
          verifySavedPlan(previewRules, previewScanner, previewPlanner, previewHasher, history, &transferCopy),
          approvePlan(verifySavedPlan, history), recoverySettings(initialSettings(defaultDestination)),
          destinationSettings(uiPreferences, defaultDestination, !verifyUi),
          fileOperations(databasePath, migrationPath, rulesPath, treeSchema, legacyTreeSchema,
              std::filesystem::path(appData.toStdWString()), recoverySettings),
          previewController(pendingFiles, createPreview, freezePreview, verifySavedPlan, approvePlan,
              history, fileOperations, rulesPath, treeSchema, defaultDestination, nullptr, legacyTreeSchema),
          recoveryController(history, nullptr, &transferJournal, &fileOperations), historyController(history, history, fileOperations),
          classificationQuery(previewRules, previewRuleEngine, previewHasher) {
        if (!verifyUi) {
            const auto restored = pendingRepository.load();
            if (restored) {
                QStringList paths;
                for (const auto& path : restored.value()) paths.append(QString::fromStdWString(path.wstring()));
                pendingFiles.restorePaths(paths);
                // 등록·제외·이동 완료로 목록이 바뀔 때 경로만 저장하는 함수.
                QObject::connect(&pendingFiles, &PendingFilesModel::countChanged,
                                 &connections, [this] {
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
        pendingAvailabilityTimer.setInterval(3000);
        QObject::connect(&pendingAvailabilityTimer, &QTimer::timeout,
                         &pendingFiles, &PendingFilesModel::refreshAvailability);
        if (!verifyUi) pendingAvailabilityTimer.start();
        if (!verifyUi) {
            previewController.setCollisionPolicy(uiPreferences.value(
                QStringLiteral("collisionPolicy"), QStringLiteral("rename")).toString());
        }
        QObject::connect(&previewController, &PreviewController::collisionPolicyChanged,
                         &connections, [this] {
            if (verifyUi) return;
            // 선택한 공통 충돌 정책을 사용자 설정에 저장하는 함수.
            uiPreferences.setValue(QStringLiteral("collisionPolicy"), previewController.collisionPolicy());
            uiPreferences.sync();
        });
        QObject::connect(&previewController, &PreviewController::destinationRootAccepted,
                         &connections, [this](const QString& root) {
            if (verifyUi) return;
            // 성공한 미리보기의 목적지만 다음 실행의 기본 경로로 기억하는 함수.
            uiPreferences.setValue(QStringLiteral("destinationRoot"), root);
            uiPreferences.sync();
        });
        // 화면의 공통 충돌 정책을 코어의 현재 설정 값으로 변환하는 함수.
        const auto currentPolicy = [this] {
            const auto key = previewController.collisionPolicy();
            return key == "abort" ? CollisionPolicy::Abort : key == "skip" ? CollisionPolicy::Skip : CollisionPolicy::Rename;
        };
        recoverySettings.setCollisionPolicy(currentPolicy());
        QObject::connect(&previewController, &PreviewController::destinationRootAccepted, &connections,
            [this](const QString& root) { recoverySettings.setHomeRoot(std::filesystem::path(root.toStdWString())); });
        QObject::connect(&previewController, &PreviewController::collisionPolicyChanged, &connections,
            [this, currentPolicy] { recoverySettings.setCollisionPolicy(currentPolicy()); });
        // 사용자 허용 목록을 실행·복구의 설정 스냅샷에 함께 반영하는 함수.
        const auto applyDestinationPermissions = [this] {
            std::vector<std::filesystem::path> roots;
            for (const auto& path : destinationSettings.allowedRoots()) roots.emplace_back(path.toStdWString());
            recoverySettings.setAllowedDestinationRoots(roots);
            previewController.setAllowedDestinationRoots(std::move(roots));
        };
        applyDestinationPermissions();
        QObject::connect(&destinationSettings, &DestinationSettingsController::allowedRootsChanged,
                         &connections, applyDestinationPermissions);
        QObject::connect(&previewController, &PreviewController::destinationRootAccepted,
                         &destinationSettings, &DestinationSettingsController::setHomeRoot);
        previewController.setTaskRunner(&backgroundTasks);
        previewController.setSortWorkflow(&fileOperations);
        historyController.setTaskRunner(&backgroundTasks);
        recoveryController.setTaskRunner(&backgroundTasks);
        QObject::connect(&historyController, &HistoryController::rollbackAttempted,
                         &previewController, &PreviewController::refreshLastUndoAfterHistoryRollback);
        QObject::connect(&historyController, &HistoryController::recoveryRefreshRequested,
                         &recoveryController, &RecoveryController::refreshRecoveryCases);
        QObject::connect(&previewController, &PreviewController::recoveryReconcileRequested,
                         &recoveryController, &RecoveryController::reconcileAndRefreshCases);
        QObject::connect(&ruleEditor, &RuleEditorController::rulesSaved,
                         &previewController, &PreviewController::refreshActiveRules);
        QObject::connect(&recoveryController, &RecoveryController::recoveryAttempted, &historyController,
            [this](const QString&) { historyController.refreshHistory(); pendingFiles.refreshAvailability(); });
        QObject::connect(&recoveryController, &RecoveryController::recoveryCompleted, &connections,
            [this](const QString& id, const QString& action) {
                if (action == "rollback") { previewController.refreshLastUndoAfterHistoryRollback(id); return; }
                const auto plan = history.loadPlan(id.toStdString());
                if (!plan || plan.value().status != PlanStatus::Completed) return;
                for (int i = pendingFiles.count() - 1; i >= 0; --i) {
                    const std::filesystem::path path(pendingFiles.filePaths()[i].toStdWString());
                    if (std::any_of(plan.value().steps.begin(), plan.value().steps.end(), [&](const MoveStep& step) {
                        return step.status == StepStatus::Applied && step.source == path;
                    })) pendingFiles.removeAt(i);
                }
            });
        if (!verifyUi && QFileInfo::exists(databasePath)) {
            recoveryController.refreshRecoveryCases();
        }
    }
};

// 외부에는 기능의 입력·조회 창구만 노출하고 내부 객체는 함께 소유하는 생성자.
FileOrganizerRuntime::FileOrganizerRuntime(FileOrganizerRuntimeConfig config, QSettings& preferences)
    : state_(std::make_unique<State>(std::move(config), preferences)) {}
// 내부 실행기가 먼저 작업을 기다린 뒤 의존성을 해제하는 소멸자.
FileOrganizerRuntime::~FileOrganizerRuntime() = default;
// 페이지 구성과 준비 동작은 기능에 맡기고 호스트에는 메뉴·입력 정보만 등록하는 함수.
Result<void> FileOrganizerRuntime::registerUi(UiPageRegistry& registry) {
    const auto title = [](const char* text) {
        // 같은 화면 문맥으로 정리 기능의 메뉴 이름을 번역하는 함수.
        return QCoreApplication::translate("OrganizerUi", text);
    };
    const auto object = [](QObject* value) {
        // 소유권을 넘기지 않고 QML에 빌려줄 입력 객체를 포장하는 함수.
        return QVariant::fromValue(value);
    };
    const auto prefix = QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/");
    return registry.addPages({
        {"organizer.sort", "file-organizer", 7, title("파일 정리"), "folder", title("파일 관리"), 10,
            QUrl(prefix + "OrganizerSortPage.qml"),
            {{"controller", object(&preview())}, {"pending", object(&pendingFiles())},
             {"recovery", object(&recovery())}, {"progress", object(&tasks())}}},
        {"organizer.rules", "file-organizer", 1, title("분류 규칙"), "rules", {}, 20,
            QUrl(prefix + "OrganizerRulesPage.qml"),
            {{"controller", object(&rules())}, {"destinations", object(&destinations())}, {"progress", object(&tasks())}}},
        {"organizer.history", "file-organizer", 4, title("작업 기록"), "history", {}, 30,
            QUrl(prefix + "OrganizerHistoryPage.qml"),
            {{"controller", object(&history())}, {"progress", object(&tasks())}}},
        {"organizer.recovery", "file-organizer", 10, title("중단 작업 확인"), "alert", {}, 31,
            QUrl(prefix + "OrganizerRecoveryPage.qml"),
            {{"controller", object(&recovery())}, {"progress", object(&tasks())}}, false},
        {"organizer.collision", "file-organizer", 6, title("이름 충돌 설정"), "settings", {}, 32,
            QUrl(prefix + "OrganizerCollisionPage.qml"), {{"controller", object(&preview())}}, false}
    }, state_->connections);
}
// 테마 설정과 감시 전환을 알지 않고 정리 관련 설정의 표시 경로와 입력만 제공하는 함수.
QVariantMap FileOrganizerRuntime::settingsUi() {
    const auto prefix = QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/");
    return {{"source", QUrl(prefix + "OrganizerSettingsSection.qml")},
        {"properties", QVariantMap{
            {"sorting", QVariant::fromValue(static_cast<QObject*>(&preview()))},
            {"destinations", QVariant::fromValue(static_cast<QObject*>(&destinations()))}}},
        {"notice", QVariantMap{{"source", QUrl(prefix + "OrganizerRecoveryNotice.qml")},
            {"properties", QVariantMap{{"controller", QVariant::fromValue(static_cast<QObject*>(&recovery()))}}}}}};
}
// 입력 검증을 통과한 초기 선택만 적용하며 기존 규칙 유지 선택은 저장본을 건드리지 않는 함수.
Result<void> FileOrganizerRuntime::configureInitialSettings(const QString& destination, const QUrl& rulesFile) {
    if (tasks().busy()) return Result<void>::failure({"setup.busy", "다른 작업이 끝난 뒤 다시 시작하기를 눌러 주세요."});
    const QFileInfo folder(destination);
    if (!folder.isAbsolute() || QDir(destination).isRoot() || (folder.exists() && !folder.isDir()))
        return Result<void>::failure({"setup.folder", "정리 폴더의 전체 경로를 확인해 주세요."});
    if (!rulesFile.isEmpty()) {
        if (rules().dirty())
            return Result<void>::failure({"setup.rules_dirty", "분류 규칙 편집기에 저장하지 않은 초안이 있어요. 먼저 저장하거나 초안을 취소해 주세요."});
        const auto imported = rules().importJsonFile(rulesFile);
        if (!imported.value("ok").toBool())
            return Result<void>::failure({"setup.rules", (imported.value("location").toString() + ": " + imported.value("reason").toString()).toStdString()});
        if (!rules().save()) {
            const auto error = rules().message();
            rules().reload();
            return Result<void>::failure({"setup.rules_save", error.toStdString()});
        }
    }
    if (!preview().setDefaultDestinationRoot(destination))
        return Result<void>::failure({"setup.folder", "기본 정리 폴더를 적용하지 못했어요."});
    if (!state_->verifyUi && state_->uiPreferences.status() != QSettings::NoError)
        return Result<void>::failure({"setup.save", "정리 설정을 저장하지 못했어요. 저장 권한을 확인해 주세요."});
    return Result<void>::success();
}
// 소유한 pendingFiles 창구를 빌려주는 함수.
PendingFilesModel& FileOrganizerRuntime::pendingFiles() { return state_->pendingFiles; }
// 대기 목록의 구체 어댑터를 노출하지 않고 조회 계약만 빌려주는 함수.
IPendingPathsQuery& FileOrganizerRuntime::pendingPaths() { return state_->pendingQueue; }
// 파일 실행 권한 없이 대기 목록 등록 계약만 빌려주는 함수.
IEnqueuePendingPaths& FileOrganizerRuntime::pendingCommands() { return state_->pendingQueue; }
// 소유한 classificationQuery 창구를 빌려주는 함수.
IClassificationQuery& FileOrganizerRuntime::classification() { return state_->classificationQuery; }
// 소유한 backgroundTasks 창구를 빌려주는 함수.
BackgroundTaskRunner& FileOrganizerRuntime::tasks() { return state_->backgroundTasks; }
// 소유한 previewController 창구를 빌려주는 함수.
PreviewController& FileOrganizerRuntime::preview() { return state_->previewController; }
// 소유한 historyController 창구를 빌려주는 함수.
HistoryController& FileOrganizerRuntime::history() { return state_->historyController; }
// 소유한 recoveryController 창구를 빌려주는 함수.
RecoveryController& FileOrganizerRuntime::recovery() { return state_->recoveryController; }
// 소유한 ruleEditor 창구를 빌려주는 함수.
RuleEditorController& FileOrganizerRuntime::rules() { return state_->ruleEditor; }
// 소유한 destinationSettings 창구를 빌려주는 함수.
DestinationSettingsController& FileOrganizerRuntime::destinations() { return state_->destinationSettings; }
}
