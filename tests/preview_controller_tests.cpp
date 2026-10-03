#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

// 테스트가 미리보기 중간에 설정을 바꿀 수 있게 하는 저장소.
class MutableSettingsRepository final : public nookbit::ISettingsRepository {
public:
    nookbit::UserSettings settings;

    // 현재 테스트 설정을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::UserSettings> load() const override {
        return nookbit::Result<nookbit::UserSettings>::success(settings);
    }

    // 테스트에서는 설정을 파일에 쓰지 않는 함수.
    [[nodiscard]] nookbit::Result<void> save(const nookbit::UserSettings&) override {
        return nookbit::Result<void>::failure({"test.read_only", "Not used in this test."});
    }
};

// 테스트에서 준비한 규칙을 그대로 반환하는 저장소.
class MemoryRulesRepository final : public nookbit::IRuleSetRepository {
public:
    nookbit::RuleSet rules;

    // 현재 테스트 규칙을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::RuleSet> load() const override {
        return nookbit::Result<nookbit::RuleSet>::success(rules);
    }
};

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트의 예제 규칙 및 Schema 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

/// 화면 테스트에서 미리보기 일꾼과 유스케이스의 수명을 함께 관리한다.
struct PreviewWorkflow {
    nookbit::JsonRuleSetRepository rules;
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine ruleEngine;
    nookbit::Planner planner;
    nookbit::QtPlanHasher hasher;
    nookbit::CreatePreviewUseCase useCase;
    nookbit::SqliteHistoryRepository history;
    nookbit::FreezePreviewUseCase freezeUseCase;
    nookbit::VerifySavedPlanUseCase verifyUseCase;
    nookbit::ApprovePlanUseCase approveUseCase;

    /// 테스트 규칙과 보호 경로로 미리보기 작업을 조립하는 생성자.
    PreviewWorkflow(QString rulesPath, QString schemaPath, std::filesystem::path appData)
        : rules(std::move(rulesPath), std::move(schemaPath)),
          planner(ruleEngine, appData),
          useCase(rules, scanner, planner, &hasher),
          history(QString::fromStdWString((appData.parent_path() / L"history.sqlite").wstring()),
                  projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql")),
          freezeUseCase(rules, scanner, planner, hasher, history),
          verifyUseCase(rules, scanner, planner, hasher, history),
          approveUseCase(verifyUseCase, history) {}

    /// 기본 예제 규칙으로 미리보기 작업을 조립하는 생성자.
    explicit PreviewWorkflow(std::filesystem::path appData)
        : PreviewWorkflow(projectPath("resources/rules/default.example.json"),
                          projectPath("schemas/rules.schema.json"), std::move(appData)) {}
};

// 감시 제안도 기존 대기 목록·고정·확인·이동·저장 흐름을 따르는지 검사하는 함수.
void testWatchedFilesUseNormalSortWorkflow() {
    QTemporaryDir directory;
    expect(directory.isValid(), "watch integration fixture should exist");
    if (!directory.isValid()) return;
    const auto incoming = directory.filePath(QStringLiteral("incoming"));
    expect(QDir().mkpath(incoming), "watched folder should exist");
    const auto first = QDir(incoming).filePath(QStringLiteral("first.jpg"));
    const auto second = QDir(incoming).filePath(QStringLiteral("second.jpg"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "watched fixture should open");
        if (!file.isOpen()) return;
        file.write("watched bytes");
    }
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(appData);
    nookbit::PendingFilesModel pending;
    nookbit::PreviewController preview(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"), appData, home);
    nookbit::JsonPendingFilesRepository queue(directory.filePath(QStringLiteral("pending-files.json")));
    // 실제 앱처럼 목록 변경 때 전체 경로만 저장하는 함수.
    QObject::connect(&pending, &nookbit::PendingFilesModel::countChanged, &pending, [&] {
        std::vector<std::filesystem::path> paths;
        for (const auto& path : pending.filePaths()) paths.emplace_back(path.toStdWString());
        expect(queue.save(paths).hasValue(), "watched queue should persist");
    });
    pending.addUrl(QUrl::fromLocalFile(first));
    preview.createPreview(home);
    expect(preview.hasPreview(), "manual first file should have a draft");
    nookbit::JsonWatchSettingsRepository watchSettings(
        directory.filePath(QStringLiteral("watch.json")), projectPath("schemas/watch.schema.json"));
    nookbit::EvaluateWatchFolderUseCase evaluate(workflow.scanner, workflow.ruleEngine, workflow.rules);
    nookbit::WatchController watch(pending, watchSettings, evaluate, incoming);
    watch.saveSettings(incoming, true, false, 10, false, true, 2, false, 1, false, 1, {});
    watch.checkNow();
    expect(watch.proposalVisible() && pending.count() == 1 && preview.hasPreview(),
           "proposal alone must not alter the queue or authorize movement");
    watch.addProposalToPending();
    expect(pending.count() == 2 && !preview.hasPreview() && !preview.hasApprovedPlan() &&
               QFile::exists(first) && QFile::exists(second),
           "acceptance must deduplicate, invalidate the old draft, and only register paths");
    const auto queued = queue.load();
    expect(queued && queued.value().size() == 2, "accepted watched files should survive restart");
    preview.confirmSort();
    expect(QFile::exists(first) && QFile::exists(second), "watched queue must not bypass confirmation preparation");
    preview.createPreview(home);
    expect(preview.prepareSort() && preview.confirmationSteps().size() == 2 &&
               !preview.hasApprovedPlan() && QFile::exists(first) && QFile::exists(second),
           "watched files must use the same frozen confirmation workflow");
#ifdef _WIN32
    QFile changed(second);
    expect(changed.open(QIODevice::Append), "watched file should reopen after confirmation preparation");
    if (!changed.isOpen()) return;
    changed.write(" edited");
    changed.close();
    preview.confirmSort();
    const auto failedQueue = queue.load();
    expect(!preview.hasPreview() && !preview.hasApprovedPlan() && pending.count() == 2 &&
               QFile::exists(first) && QFile::exists(second) && failedQueue && failedQueue.value().size() == 2,
           "failed watched confirmation must preserve both queue entries on disk and both source files");
    preview.createPreview(home);
    expect(preview.prepareSort(), "changed watched file needs a fresh preview and confirmation");
    preview.confirmSort();
    expect(!QFile::exists(first) && !QFile::exists(second) && pending.count() == 0 && preview.canUndo(),
           "explicit confirmation should move watched files and remove their queue entries");
    const auto afterMove = queue.load();
    expect(afterMove && afterMove.value().empty(), "moved watched files must not return in the saved queue");
    preview.undoLastMove();
    expect(QFile::exists(first) && QFile::exists(second), "watched sort must remain undoable");
#endif
}

// 편집 초안은 정리에 영향이 없고 전체 저장한 규칙만 다음 미리보기에 적용되는지 검사하는 함수.
void testSavedEditorRulesReachMainPreview() {
    QTemporaryDir directory;
    expect(directory.isValid(), "rule-to-preview directory should be available");
    if (!directory.isValid()) return;

    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "rule-to-preview source should open");
    if (!file.isOpen()) return;
    file.write("preview only");
    file.close();

    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "default v3 rules should load");
    if (!initial) return;

    const auto activePath = directory.filePath(QStringLiteral("rules.json"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow workflow(activePath, schema, appData);
    expect(workflow.rules.save(initial.value()).hasValue(), "active v3 rules should initialize");
    nookbit::RuleEditorController editor(workflow.rules, workflow.rules);
    nookbit::PendingFilesModel pending;
    nookbit::PreviewController preview(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, activePath, schema, appData, home);
    QObject::connect(&editor, &nookbit::RuleEditorController::rulesSaved,
                     &preview, &nookbit::PreviewController::refreshActiveRules);
    pending.addUrl(QUrl::fromLocalFile(source));

    const auto originalLabel = preview.activeRuleLabel();
    expect(originalLabel.contains(QStringLiteral("default")),
           "main page should identify the active saved rule set");
    expect(editor.edit(QStringLiteral("0"), QStringLiteral("images"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("jpg"),
                       QStringLiteral("MiB"), QStringLiteral("Pictures/Edited")),
           "editor draft should change the image destination");
    preview.createPreview(home);
    expect(preview.steps().size() == 1, "unsaved draft should still make an active-rule preview");
    if (preview.steps().size() == 1) {
        expect(!preview.steps().front().toMap().value(QStringLiteral("destination")).toString()
                    .contains(QStringLiteral("Pictures/Edited")),
               "unsaved draft must not affect the main preview");
    }

    expect(editor.save(), "editor should save the new active rules");
    expect(!preview.hasPreview(), "saving rules should invalidate the old preview");
    expect(preview.activeRuleLabel() != originalLabel,
           "main page should refresh the applied rule version after save");
    preview.createPreview(home);
    expect(preview.steps().size() == 1, "saved rules should make a new preview");
    if (preview.steps().size() == 1) {
        expect(preview.steps().front().toMap().value(QStringLiteral("destination")).toString()
                    .contains(QStringLiteral("Pictures/Edited")),
               "new preview should use the rule saved by the editor");
    }
    expect(QFile::exists(source), "rule-to-preview test must not move the source");
}

// 기본 페이지가 이동·충돌·미분류·제외를 파일마다 구분해 표시하는지 검사하는 함수.
void testEveryPreviewOutcomeIsVisible() {
    QTemporaryDir directory;
    expect(directory.isValid(), "preview outcome directory should exist");
    if (!directory.isValid()) return;

    const auto home = directory.filePath(QStringLiteral("organized"));
    expect(QDir().mkpath(home + QStringLiteral("/Pictures")),
           "preview outcome destination should exist");
    for (const auto& name : {QStringLiteral("photo.jpg"), QStringLiteral("already.jpg"),
                             QStringLiteral("notes.txt"), QStringLiteral("ignore.tmp")}) {
        QFile source(directory.filePath(name));
        expect(source.open(QIODevice::WriteOnly), "preview outcome source should open");
        if (!source.isOpen()) return;
        source.write("source bytes");
    }
    QFile occupied(home + QStringLiteral("/Pictures/already.jpg"));
    expect(occupied.open(QIODevice::WriteOnly), "collision fixture should open");
    if (!occupied.isOpen()) return;
    occupied.write("existing bytes");
    occupied.close();

    nookbit::RuleSet rules;
    rules.id = "preview-outcomes";
    rules.version = "2026-10-02T00:00:00Z";
    rules.schemaVersion = 3;
    nookbit::RuleNode images;
    images.id = "images";
    images.atomicCondition = nookbit::ExtensionPredicate{{"jpg"}};
    images.destination = nookbit::RuleDestination{L"Pictures", nookbit::CollisionPolicy::Skip};
    nookbit::RuleNode excluded;
    excluded.id = "ignore-temp";
    excluded.atomicCondition = nookbit::ExtensionPredicate{{"tmp"}};
    excluded.exclude = true;
    rules.nodes = {images, excluded};

    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(rulesPath, schema, appData);
    expect(workflow.rules.save(rules).hasValue(), "preview outcome rules should save");
    nookbit::PendingFilesModel pending;
    for (const auto& name : {QStringLiteral("photo.jpg"), QStringLiteral("already.jpg"),
                             QStringLiteral("notes.txt"), QStringLiteral("ignore.tmp")}) {
        pending.addUrl(QUrl::fromLocalFile(directory.filePath(name)));
    }
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, rulesPath, schema, appData, home);
    controller.createPreview(home);
    expect(controller.steps().size() == 2 && controller.outcomes().size() == 4,
           "all four decisions should appear without adding non-moves to executable steps");
    QHash<QString, QVariantMap> rows;
    for (const auto& value : controller.outcomes()) {
        const auto row = value.toMap();
        rows.insert(row.value(QStringLiteral("fileName")).toString(), row);
    }
    expect(rows.value(QStringLiteral("photo.jpg")).value(QStringLiteral("statusKey")) == "planned" &&
               !rows.value(QStringLiteral("photo.jpg")).value(QStringLiteral("destination")).toString().isEmpty(),
           "matched file should show its proposed destination");
    expect(rows.value(QStringLiteral("already.jpg")).value(QStringLiteral("statusKey")) == "skipped" &&
               rows.value(QStringLiteral("already.jpg")).value(QStringLiteral("reason")).toString().contains(
                   QStringLiteral("같은 이름")),
           "colliding file should explain why it stays put");
    expect(rows.value(QStringLiteral("notes.txt")).value(QStringLiteral("statusKey")) == "unmatched" &&
               rows.value(QStringLiteral("notes.txt")).value(QStringLiteral("destination")).toString().isEmpty(),
           "unmatched file should show no destination");
    expect(rows.value(QStringLiteral("ignore.tmp")).value(QStringLiteral("statusKey")) == "excluded" &&
               rows.value(QStringLiteral("ignore.tmp")).value(QStringLiteral("rule")) == "ignore-temp",
           "explicitly excluded file should show its matching rule");
    expect(controller.warnings().isEmpty(),
           "expected unmatched and excluded results should not repeat as generic warnings");
    expect(QFile::exists(directory.filePath(QStringLiteral("photo.jpg"))) &&
               QFile::exists(directory.filePath(QStringLiteral("already.jpg"))),
           "preview outcome display must not move files");
}

// 공통 충돌 정책이 승인·실행까지 전달되고 규칙의 개별 설정이 우선하는지 검사하는 함수.
void testCommonCollisionPolicyWorkflow() {
    QTemporaryDir directory;
    expect(directory.isValid(), "collision policy directory should exist");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    const auto home = directory.filePath(QStringLiteral("organized"));
    expect(QDir().mkpath(home + QStringLiteral("/Pictures")), "collision target should exist");
    const auto occupiedPath = home + QStringLiteral("/Pictures/photo.jpg");
    for (const auto& path : {source, occupiedPath}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "collision fixture should open");
        if (!file.isOpen()) return;
        file.write(path == source ? "source bytes" : "existing bytes");
    }

    nookbit::RuleSet rules;
    rules.id = "collision-inheritance";
    rules.version = "2026-10-02T00:00:00Z";
    rules.schemaVersion = 3;
    nookbit::RuleNode images;
    images.id = "images";
    images.atomicCondition = nookbit::ExtensionPredicate{{"jpg"}};
    images.destination = nookbit::RuleDestination{L"Pictures", std::nullopt};
    rules.nodes = {images};
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(rulesPath, schema, appData);
    expect(workflow.rules.save(rules).hasValue(), "inherited collision rules should save");
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, rulesPath, schema, appData, home);

    controller.createPreview(home);
    const auto renamedPath = home + QStringLiteral("/Pictures/photo (1).jpg");
    expect(controller.steps().size() == 1 &&
               QDir::fromNativeSeparators(controller.steps().front().toMap().value(
                   QStringLiteral("destination")).toString()) == renamedPath,
           "default rename policy should propose a free filename");
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "rename preview should be approvable");

    controller.setCollisionPolicy(QStringLiteral("skip"));
    expect(!controller.hasPreview() && !controller.hasFrozenPlan() && !controller.hasApprovedPlan(),
           "changing policy should invalidate the old draft and approval");
    controller.createPreview(home);
    expect(controller.hasPreview() && !controller.hasMovableSteps() &&
               controller.outcomes().size() == 1 &&
               controller.outcomes().front().toMap().value(QStringLiteral("statusKey")).toString() ==
                   QStringLiteral("skipped"),
           "inherited skip policy should keep a colliding file in place");

    controller.setCollisionPolicy(QStringLiteral("abort"));
    controller.createPreview(home);
    expect(!controller.hasPreview() && controller.message().contains(QStringLiteral("충돌")) &&
               QFile::exists(source),
           "inherited abort policy should stop before any file movement");

    rules.nodes.front().destination->collisionPolicy = nookbit::CollisionPolicy::Rename;
    expect(workflow.rules.save(rules).hasValue(), "explicit collision policy should save");
    controller.refreshActiveRules();
    controller.createPreview(home);
    expect(controller.hasMovableSteps(), "rule-specific rename should override common abort");
    expect(controller.prepareSort(), "sort action should prepare explicit confirmation");
    expect(controller.hasFrozenPlan() && !controller.hasApprovedPlan() && QFile::exists(source),
           "preparing confirmation must freeze without approving or moving");
    expect(controller.hasFrozenPlan() && controller.verifySavedPlan(),
           "selected common policy should remain consistent through freeze and verification");
#ifdef _WIN32
    controller.confirmSort();
    expect(!QFile::exists(source) && QFile::exists(renamedPath) && QFile::exists(occupiedPath),
           "execution should preserve the occupied file and use the rule-specific renamed destination");
    QFile occupied(occupiedPath);
    expect(occupied.open(QIODevice::ReadOnly) && occupied.readAll() == "existing bytes",
           "collision handling must preserve existing destination contents");
    occupied.close();
    controller.undoLastMove();
    expect(QFile::exists(source) && !QFile::exists(renamedPath), "policy workflow should remain undoable");
#endif
}

// 최종 확인 취소·변경 감지·중복 확인이 파일을 잘못 이동시키지 않는지 검사하는 함수.
void testSortConfirmationSafety() {
    QTemporaryDir directory;
    expect(directory.isValid(), "confirmation directory should exist");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "confirmation source should open");
    if (!file.isOpen()) return;
    file.write("before");
    file.close();
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(appData);
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"), appData, home);
    controller.confirmSort();
    expect(QFile::exists(source) && !controller.hasApprovedPlan(),
           "confirmation without preparation must do nothing");
    pending.restorePaths({source});
    expect(!controller.hasPreview() && !controller.hasFrozenPlan() && !controller.hasApprovedPlan(),
           "restored queue must not restore any preview or authorization");
    controller.createPreview(home);
    expect(controller.prepareSort() && controller.awaitingConfirmation(),
           "valid preview should open confirmation");
    expect(controller.confirmationSteps().size() == 1 &&
               QDir::fromNativeSeparators(controller.confirmationSteps().front().toMap().value(
                   QStringLiteral("source")).toString()) == QDir::fromNativeSeparators(source),
           "confirmation should show the frozen source");
    controller.cancelSort();
    controller.confirmSort();
    expect(!controller.awaitingConfirmation() && !controller.hasApprovedPlan() && QFile::exists(source),
           "cancelled confirmation cannot authorize movement");
    expect(controller.prepareSort(), "cancelled frozen plan may be rechecked and offered again");
    expect(file.open(QIODevice::Append), "confirmation source should reopen");
    if (!file.isOpen()) return;
    file.write(" changed");
    file.close();
    controller.confirmSort();
    expect(!controller.hasPreview() && !controller.awaitingConfirmation() &&
               !controller.hasApprovedPlan() && QFile::exists(source) && !QFile::exists(home),
           "changed source must invalidate confirmation without moving");
    controller.createPreview(home);
    expect(controller.prepareSort(), "fresh preview should prepare again");
    controller.setCollisionPolicy(QStringLiteral("skip"));
    controller.confirmSort();
    expect(!controller.awaitingConfirmation() && QFile::exists(source),
           "setting changes must invalidate pending confirmation");
    controller.createPreview(home);
    expect(controller.prepareSort(), "new policy preview should prepare");
    pending.removeAt(0);
    controller.confirmSort();
    expect(!controller.awaitingConfirmation() && QFile::exists(source),
           "pending list changes must invalidate confirmation");
    pending.restorePaths({source, directory.filePath(QStringLiteral("missing.jpg"))});
    controller.createPreview(home);
    expect(pending.count() == 2 && !controller.hasPreview() &&
               controller.message().contains(QStringLiteral("찾을 수 없는 파일")),
           "missing restored files must remain visible and block preview until resolved");
}

// 대기 파일이 미리보기로 이어지고 원본은 그대로 남는지 검사하는 함수.
void testPreviewFromPendingFiles() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;

    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "source fixture should open");
    if (!file.isOpen()) return;
    file.write("original content");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = directory.filePath(QStringLiteral("app-data"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(appData.toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(appData.toStdWString()), home);
    QString acceptedDestination;
    QObject::connect(&controller, &nookbit::PreviewController::destinationRootAccepted,
                     &controller, [&acceptedDestination](const QString& root) {
        acceptedDestination = root;
    });
    controller.createPreview(home);

    expect(controller.hasPreview(), "preview should be available");
    expect(acceptedDestination == home,
           "successful preview should offer its destination as the next default");
    expect(controller.steps().size() == 1, "image should create one planned step");
    if (controller.steps().size() == 1) {
        const auto step = controller.steps().at(0).toMap();
        expect(step.value(QStringLiteral("rule")).toString() == QStringLiteral("images"),
               "image rule should be displayed");
        expect(step.value(QStringLiteral("destination")).toString().startsWith(home),
               "destination should stay under selected home");
        expect(step.value(QStringLiteral("destination")).toString().contains(
                   QStringLiteral("Pictures")),
               "destination should include the image category");
    }
    expect(QFile::exists(source), "preview must not move the original file");
    expect(!QFile::exists(home), "preview must not create the destination directory");

    controller.freezePreview();
    expect(controller.hasFrozenPlan(), "unchanged preview should be frozen");
    expect(QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "frozen plan should be saved to SQLite");
    nookbit::SqliteHistoryRepository stored(
        directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    const auto plans = stored.incompletePlans();
    expect(plans.hasValue() && plans.value().size() == 1,
           "stored plan should be readable after saving");
    if (plans && plans.value().size() == 1) {
        const auto& storedPlan = plans.value().front();
        expect(plans.value().front().status == nookbit::PlanStatus::Planned,
               "stored plan must be Planned, not Approved");
        expect(plans.value().front().planHash.size() == 64,
               "stored plan should contain a SHA-256 digest");
        expect(plans.value().front().steps.size() == 1 &&
                   plans.value().front().steps.front().source ==
                       std::filesystem::path(source.toStdWString()),
               "stored step should preserve the selected source");
        nookbit::QtPlanHasher hasher;
        const auto expectedHash = hasher.sha256(
            nookbit::PlanSignature::approvalPayload(storedPlan));
        expect(storedPlan.planHash == expectedHash,
               "stored plan should round-trip without changing its approval hash");
    }
    expect(controller.verifySavedPlan(), "unchanged saved plan should pass revalidation");
    expect(QFile::exists(source) && !QFile::exists(home),
           "saving a plan must not move files or create the destination");

    pending.removeAt(0);
    expect(!controller.hasPreview() && controller.steps().isEmpty(),
           "changing the pending list should clear a stale preview");

    pending.addUrl(QUrl::fromLocalFile(source));
    controller.createPreview(QStringLiteral("relative/path"));
    expect(!controller.hasPreview() && !controller.message().isEmpty(),
           "relative destination should be rejected");
    expect(QFile::exists(source), "invalid preview must not change the original file");
}

// 트리 JSON의 리프 결과가 기존 미리보기·계획 저장 흐름에 연결되는지 검사하는 함수.
void testTreeRulesReachPreview() {
    QTemporaryDir directory;
    expect(directory.isValid(), "tree-preview directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "tree-preview source should open");
    if (!file.isOpen()) return;
    file.write("small photo");
    file.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    const auto rulesPath = projectPath("resources/rules/tree.example.json");
    const auto schemaPath = projectPath("schemas/rules-tree.schema.json");
    PreviewWorkflow workflow(rulesPath, schemaPath, appData);
    nookbit::PreviewController controller(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history, rulesPath, schemaPath, appData, home);
    controller.createPreview(home);
    expect(controller.hasPreview() && controller.steps().size() == 1,
           "tree rules should produce one preview step");
    if (controller.steps().size() != 1) {
        return;
    }
    const auto step = controller.steps().front().toMap();
    expect(step.value(QStringLiteral("rule")).toString() == QStringLiteral("other-images") &&
               QDir::fromNativeSeparators(step.value(QStringLiteral("destination")).toString()).contains(
                   QStringLiteral("Pictures/Other")),
           "tree else leaf should supply the preview destination");
    controller.freezePreview();
    expect(controller.hasFrozenPlan() && controller.verifySavedPlan(),
           "tree plan should remain valid after saving and rechecking");
    expect(QFile::exists(source), "tree preview must not move the source");
}

// 저장 뒤 원본이 바뀌면 재검사를 통과하지 못하는지 검사하는 함수.
void testChangedFileAfterSaveFailsVerification() {
    QTemporaryDir directory;
    expect(directory.isValid(), "post-save directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "post-save source should open");
    if (!file.isOpen()) return;
    file.write("before");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    controller.createPreview(home);
    controller.freezePreview();
    expect(controller.hasFrozenPlan(), "post-save plan should be stored");
    expect(file.open(QIODevice::Append), "post-save source should reopen");
    if (!file.isOpen()) return;
    file.write(" changed");
    file.close();
    expect(!controller.verifySavedPlan(), "changed source must fail saved-plan verification");
    expect(QFile::exists(source) && !QFile::exists(home),
           "failed verification must not move or copy files");
}

// 저장된 단계 경로가 DB에서 바뀌면 계획 해시 검사로 거절하는지 확인하는 함수.
void testTamperedSavedPlanFailsVerification() {
    QTemporaryDir directory;
    expect(directory.isValid(), "tamper-test directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "tamper-test source should open");
    if (!file.isOpen()) return;
    file.write("image");
    file.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto dbPath = directory.filePath(QStringLiteral("history.sqlite"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    controller.createPreview(home);
    controller.freezePreview();
    expect(controller.verifySavedPlan(), "untampered plan should pass first");

    const QString connection = QStringLiteral("nookbit-tamper-test");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(dbPath);
        expect(database.open(), "tamper-test database should open");
        if (database.isOpen()) {
            QSqlQuery query(database);
            expect(query.exec(QStringLiteral(
                       "UPDATE steps SET destination_path = 'C:/unexpected/photo.jpg'")),
                   "tamper-test destination should be modified");
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    expect(!controller.verifySavedPlan(), "modified DB step must fail its saved hash");
    expect(QFile::exists(source) && !QFile::exists(home),
           "tamper detection must not change source or destination");
}

// 같은 버전의 규칙 내용이 바뀌어도 오래된 미리보기를 거절하는지 검사하는 함수.
void testChangedRulesCannotBeFrozen() {
    QTemporaryDir directory;
    expect(directory.isValid(), "rule-change directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "rule-change source should open");
    if (!file.isOpen()) return;
    file.write("image");
    file.close();

    QFile example(projectPath("resources/rules/default.example.json"));
    expect(example.open(QIODevice::ReadOnly), "example rules should open");
    if (!example.isOpen()) return;
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    QFile rulesFile(rulesPath);
    expect(rulesFile.open(QIODevice::WriteOnly), "rule-change copy should open");
    if (!rulesFile.isOpen()) return;
    rulesFile.write(example.readAll());
    rulesFile.close();
    example.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(rulesPath, projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        rulesPath, projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    controller.createPreview(home);
    expect(controller.hasPreview(), "rule-change preview should be available");

    expect(rulesFile.open(QIODevice::ReadOnly), "rule-change copy should reopen");
    if (!rulesFile.isOpen()) return;
    auto object = QJsonDocument::fromJson(rulesFile.readAll()).object();
    rulesFile.close();
    auto rules = object.value(QStringLiteral("rules")).toArray();
    auto first = rules.at(0).toObject();
    auto then = first.value(QStringLiteral("then")).toObject();
    then.insert(QStringLiteral("destination"), QStringLiteral("Other/{yyyy}/{MM}"));
    first.insert(QStringLiteral("then"), then);
    rules.replace(0, first);
    object.insert(QStringLiteral("rules"), rules);
    expect(rulesFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "changed rules should be writable");
    if (!rulesFile.isOpen()) return;
    rulesFile.write(QJsonDocument(object).toJson());
    rulesFile.close();

    controller.freezePreview();
    expect(!controller.hasFrozenPlan() && !controller.hasPreview(),
           "same-version rule edits should invalidate the preview");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "changed rules must not create a stored plan");
    expect(QFile::exists(source), "rule-change rejection must leave the source in place");
}

// 분류 결과가 같아도 설정 자체가 바뀌면 고정을 거절하는지 검사하는 함수.
void testChangedSettingsCannotBeFrozen() {
    QTemporaryDir directory;
    expect(directory.isValid(), "settings-change directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "settings-change source should open");
    if (!file.isOpen()) return;
    file.write("image");
    file.close();

    MutableSettingsRepository settings;
    settings.settings.homeRoot = std::filesystem::path(
        directory.filePath(QStringLiteral("organized")).toStdWString());
    MemoryRulesRepository rules;
    nookbit::ClassificationRule rule;
    rule.id = "images";
    rule.enabled = true;
    rule.priority = 100;
    rule.extensions = {"jpg"};
    rule.destinationRelativePath = L"Pictures";
    rules.rules = {"default", "rules-1", {rule}};

    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, std::filesystem::path(
        directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::QtPlanHasher hasher;
    nookbit::SqliteHistoryRepository history(
        directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher);
    const auto sourcePath = std::filesystem::path(source.toStdWString());
    const auto draft = preview.execute({"rules-1", {sourcePath}});
    expect(draft.hasValue(), "settings-change draft should be created");
    if (!draft) return;

    settings.settings.recursiveScan = true;
    nookbit::FreezePreviewUseCase freeze(rules, scanner, planner, hasher, history);
    const auto result = freeze.execute(draft.value(), {sourcePath}, settings.settings);
    expect(!result.hasValue() && result.error().code == "freeze.settings_changed",
           "setting changes must be rejected even when planned destination is unchanged");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "changed settings must not create a stored plan");
    expect(QFile::exists(source), "settings-change rejection must leave the source in place");

    settings.settings.recursiveScan = false;
    const auto frozen = freeze.execute(draft.value(), {sourcePath}, settings.settings);
    expect(frozen.hasValue(), "restored settings should allow storing the plan");
    if (!frozen) return;
    nookbit::VerifySavedPlanUseCase verify(rules, scanner, planner, hasher, history);
    expect(verify.execute(frozen.value().id, {sourcePath}, settings.settings).hasValue(),
           "unchanged settings and rules should verify");

    settings.settings.recursiveScan = true;
    const auto changedSettings = verify.execute(
        frozen.value().id, {sourcePath}, settings.settings);
    expect(!changedSettings.hasValue() && changedSettings.error().code == "verify.settings_changed",
           "post-save setting changes should fail verification");
    settings.settings.recursiveScan = false;
    rules.rules.rules.front().destinationRelativePath = L"Other";
    const auto changedRules = verify.execute(
        frozen.value().id, {sourcePath}, settings.settings);
    expect(!changedRules.hasValue() && changedRules.error().code == "verify.rules_changed",
           "post-save rule changes should fail verification even with the same version");
}

// 미리보기 뒤 원본 파일이 바뀌면 저장을 거부하는지 검사하는 함수.
void testChangedFileCannotBeFrozen() {
    QTemporaryDir directory;
    expect(directory.isValid(), "stale-test directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "stale-test source should open");
    if (!file.isOpen()) return;
    file.write("before");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    controller.createPreview(home);
    expect(controller.hasPreview(), "stale-test preview should be available");
    expect(file.open(QIODevice::Append), "stale-test source should reopen");
    if (!file.isOpen()) return;
    file.write(" changed");
    file.close();

    controller.freezePreview();
    expect(!controller.hasFrozenPlan() && !controller.hasPreview(),
           "modified file should invalidate the preview");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "stale preview must not create a saved plan");
    expect(QFile::exists(source), "stale rejection must leave the source in place");
}

// 화면에서 명시적 승인 뒤 파일 하나를 이동하고 원래 위치로 되돌리는 흐름.
void testSingleFileApproveMoveUndo() {
#ifdef _WIN32
    QTemporaryDir directory;
    expect(directory.isValid(), "move-flow directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "move-flow source should open");
    if (!file.isOpen()) return;
    file.write("keep this content");
    file.close();

    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    controller.createPreview(home);
    expect(controller.hasPreview() && controller.steps().size() == 1,
           "single file should create one preview step");
    if (controller.steps().size() != 1) return;
    const auto destination = controller.steps().front().toMap()
                                 .value(QStringLiteral("destination")).toString();
    controller.moveApprovedFile();
    expect(QFile::exists(source) && !QFile::exists(destination),
           "move without approval must leave the file untouched");
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "explicit approval should be recorded");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "approval alone must not move the file");
    controller.moveApprovedFile();
    expect(!QFile::exists(source) && QFile::exists(destination),
           "approved file should move to its planned destination");
    expect(pending.count() == 0 && controller.canUndo() && !controller.hasApprovedPlan(),
           "completed move should leave an undo action and clear the pending item");
    controller.undoLastMove();
    expect(QFile::exists(source) && !QFile::exists(destination) && !controller.canUndo(),
           "undo should restore the original file without leaving an active undo action");
    QFile restored(source);
    expect(restored.open(QIODevice::ReadOnly) && restored.readAll() == "keep this content",
           "undo should preserve the file contents");
#endif
}

// 여러 대기 파일을 한 계획으로 승인하고 되돌릴 수 있는지 검사하는 함수.
void testMultipleFilesCanBeMoved() {
    QTemporaryDir directory;
    expect(directory.isValid(), "multi-file directory should be available");
    if (!directory.isValid()) return;
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    const auto unmatched = directory.filePath(QStringLiteral("notes.txt"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "multi-file source should open");
        if (!file.isOpen()) return;
        file.write("content");
    }
    QFile noteFile(unmatched);
    expect(noteFile.open(QIODevice::WriteOnly) && noteFile.write("keep") == 4,
           "unmatched file should be written");
    noteFile.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(first));
    pending.addUrl(QUrl::fromLocalFile(second));
    pending.addUrl(QUrl::fromLocalFile(unmatched));
    const auto home = directory.filePath(QStringLiteral("organized"));
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    controller.createPreview(home);
    const auto previewRows = controller.steps();
    expect(previewRows.size() == 2, "both classified files should appear in preview");
    if (previewRows.size() != 2) return;
    const auto firstDestination = previewRows.at(0).toMap().value(QStringLiteral("destination")).toString();
    const auto secondDestination = previewRows.at(1).toMap().value(QStringLiteral("destination")).toString();
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "multi-file plan should be approved from the screen");
    expect(QFile::exists(first) && QFile::exists(second),
           "multi-file approval must not move either file");
    controller.moveApprovedFile();
    expect(!QFile::exists(first) && !QFile::exists(second) &&
               QFile::exists(firstDestination) && QFile::exists(secondDestination) &&
               QFile::exists(unmatched) && pending.count() == 1 && controller.canUndo(),
           "approved multi-file plan should move only classified files and offer undo");
    controller.undoLastMove();
    expect(QFile::exists(first) && QFile::exists(second) &&
               !QFile::exists(firstDestination) && !QFile::exists(secondDestination) &&
               QFile::exists(unmatched) && pending.count() == 1 && !controller.canUndo(),
           "multi-file undo should restore both files");
}

// 기록에서 마지막 이동을 되돌리면 계획 화면의 되돌리기 버튼도 꺼지는지 검사하는 함수.
void testHistoryUndoUpdatesLastMove() {
#ifdef _WIN32
    QTemporaryDir directory;
    expect(directory.isValid(), "shared-undo directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "shared-undo source should open");
    if (!file.isOpen()) return;
    file.write("original bytes");
    file.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    PreviewWorkflow workflow(appData);
    nookbit::PreviewController preview(
        pending, workflow.useCase, workflow.freezeUseCase, workflow.verifyUseCase,
        workflow.approveUseCase, workflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"), appData, home);
    nookbit::HistoryController history(workflow.history, appData, home);
    QObject::connect(&history, &nookbit::HistoryController::rollbackAttempted,
                     &preview, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    preview.createPreview(home);
    preview.freezePreview();
    preview.approvePlan();
    preview.moveApprovedFile();
    expect(preview.canUndo(), "last move should offer undo before history rollback");
    history.refreshHistory();
    if (history.historyEntries().isEmpty()) return;
    history.selectHistoryPlan(history.historyEntries().front().toMap()
                                  .value(QStringLiteral("planId")).toString());
    history.undoSelectedHistory();
    expect(QFile::exists(source) && !preview.canUndo(),
           "history rollback should restore file and clear last-move undo");
#endif
}

// 앱을 다시 연 뒤 과거 작업을 골라 안전하게 되돌리는 흐름을 검사하는 함수.
void testHistoryUndoAfterRestart() {
#ifdef _WIN32
    QTemporaryDir directory;
    expect(directory.isValid(), "history directory should be available");
    if (!directory.isValid()) return;
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly) && file.write("original bytes") == 14,
               "history source should be written");
    }
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto dbPath = directory.filePath(QStringLiteral("history.sqlite"));
    const auto rulesPath = projectPath("resources/rules/default.example.json");
    const auto schemaPath = projectPath("schemas/rules.schema.json");
    const auto migrationPath = projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
    const auto appData = std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString());
    QString firstPlanId;
    QString firstDestination;
    QString secondDestination;
    {
        nookbit::PendingFilesModel pending;
        PreviewWorkflow previewWorkflow(rulesPath, schemaPath, appData);
        nookbit::PreviewController controller(
            pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
            previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
            previewWorkflow.history,
            rulesPath, schemaPath, appData, home);
        nookbit::HistoryController historyController(previewWorkflow.history, appData, home);
        QObject::connect(&historyController, &nookbit::HistoryController::rollbackAttempted,
                         &controller, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
        pending.addUrl(QUrl::fromLocalFile(first));
        controller.createPreview(home);
        firstDestination = controller.steps().front().toMap().value(QStringLiteral("destination")).toString();
        controller.freezePreview();
        controller.approvePlan();
        controller.moveApprovedFile();
        historyController.refreshHistory();
        expect(historyController.historyEntries().size() == 1, "first move should appear in history");
        if (historyController.historyEntries().isEmpty()) return;
        firstPlanId = historyController.historyEntries().front().toMap().value(QStringLiteral("planId")).toString();

        pending.addUrl(QUrl::fromLocalFile(second));
        controller.createPreview(home);
        secondDestination = controller.steps().front().toMap().value(QStringLiteral("destination")).toString();
        controller.freezePreview();
        controller.approvePlan();
        controller.moveApprovedFile();
    }
    expect(QFile::exists(firstDestination) && QFile::exists(secondDestination),
           "both files should stay moved after closing the first controller");
    nookbit::PendingFilesModel pendingAfterRestart;
    PreviewWorkflow restartedWorkflow(rulesPath, schemaPath, appData);
    nookbit::PreviewController restarted(
        pendingAfterRestart, restartedWorkflow.useCase, restartedWorkflow.freezeUseCase,
        restartedWorkflow.verifyUseCase, restartedWorkflow.approveUseCase,
        restartedWorkflow.history,
        rulesPath, schemaPath, appData, home);
    nookbit::HistoryController restartedHistory(restartedWorkflow.history, appData, home);
    QObject::connect(&restartedHistory, &nookbit::HistoryController::rollbackAttempted,
                     &restarted, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    restartedHistory.refreshHistory();
    expect(restartedHistory.historyEntries().size() == 2, "restart should reload both past moves");
    restartedHistory.selectHistoryPlan(firstPlanId);
    expect(restartedHistory.canUndoSelectedHistory() && restartedHistory.selectedHistoryFileCount() == 1,
           "older selected move should offer undo");
    expect(restartedHistory.historySteps().size() == 1,
           "older selected move should show its file");
    if (restartedHistory.historySteps().size() == 1) {
        const auto shownSource = restartedHistory.historySteps().front().toMap()
                                     .value(QStringLiteral("source")).toString();
        expect(QDir::fromNativeSeparators(shownSource) == QDir::fromNativeSeparators(first),
               "older selected move should show the original path");
    }

    QFile occupied(first);
    expect(occupied.open(QIODevice::WriteOnly) && occupied.write("new occupant") == 12,
           "source collision should be created");
    occupied.close();
    restartedHistory.undoSelectedHistory();
    expect(QFile::exists(firstDestination) && QFile::exists(first) &&
               restartedHistory.historyMessage().contains(QStringLiteral("되돌리지 못했어요")),
           "history undo must refuse an occupied original path");
    expect(QFile::remove(first), "collision fixture should be removed");
    restartedHistory.selectHistoryPlan(firstPlanId);
    restartedHistory.undoSelectedHistory();
    expect(QFile::exists(first) && !QFile::exists(firstDestination) &&
               QFile::exists(secondDestination) && !QFile::exists(second),
           "chosen older move should undo without touching the newer move");
    restartedHistory.selectHistoryPlan(firstPlanId);
    expect(!restartedHistory.canUndoSelectedHistory(), "already undone history must not be undoable again");
#endif
}

// 중단 작업이 화면에 나타나고 확인 후 기록만 정리되는지 검사하는 함수.
void testRecoveryCasesInController() {
    QTemporaryDir directory;
    expect(directory.isValid(), "recovery-screen directory should be available");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("photo.jpg"));
    QFile sourceFile(source);
    expect(sourceFile.open(QIODevice::WriteOnly), "recovery-screen source should open");
    if (!sourceFile.isOpen()) return;
    sourceFile.write("original bytes");
    sourceFile.close();
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(source));
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto databasePath = directory.filePath(QStringLiteral("history.sqlite"));
    const auto migrationPath = projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
    PreviewWorkflow previewWorkflow(std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()));
    nookbit::PreviewController controller(
        pending, previewWorkflow.useCase, previewWorkflow.freezeUseCase,
        previewWorkflow.verifyUseCase, previewWorkflow.approveUseCase,
        previewWorkflow.history,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home);
    nookbit::RecoveryController recoveryController(previewWorkflow.history);
    QObject::connect(&controller, &nookbit::PreviewController::recoveryReconcileRequested,
                     &recoveryController, &nookbit::RecoveryController::reconcileAndRefreshCases);
    controller.createPreview(home);
    controller.freezePreview();
    controller.approvePlan();
    expect(controller.hasApprovedPlan(), "recovery-screen plan should be approved");
    if (!controller.hasApprovedPlan()) return;
    nookbit::SqliteHistoryRepository history(databasePath, migrationPath);
    auto plans = history.incompletePlans();
    expect(plans.hasValue() && plans.value().size() == 1,
           "recovery-screen plan should be readable");
    if (!plans || plans.value().size() != 1) return;
    const auto& plan = plans.value().front();
    const auto& step = plan.steps.front();
    expect(history.appendEvent(
               nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue(),
           "recovery-screen execution should begin");
    expect(history.appendEvent(
               nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue(),
           "recovery-screen intent should be recorded");
    const auto destination = QString::fromStdWString(step.destination.wstring());
    expect(QDir().mkpath(QFileInfo(destination).absolutePath()),
           "recovery-screen destination parent should exist");
    expect(QFile::copy(source, destination), "recovery-screen copy should be created");
    nookbit::FilesystemScanner scanner;
    nookbit::QtPlanHasher hasher;
    nookbit::RecoveryService recovery(history, scanner, hasher);
    auto reconciled = recovery.reconcile();
    expect(reconciled.hasValue() && reconciled.value().needsAttention.size() == 1,
           "ambiguous recovery-screen plan should require attention");
    recoveryController.refreshRecoveryCases();
    expect(recoveryController.recoveryCases().size() == 1 &&
               !recoveryController.recoveryCases().front().toMap().value(QStringLiteral("canResolve")).toBool(),
           "screen should show ambiguity without offering confirmation");
    expect(QFile::remove(destination), "test copy should be removed");
    recoveryController.refreshRecoveryCases();
    expect(recoveryController.recoveryCases().size() == 1 &&
               recoveryController.recoveryCases().front().toMap().value(QStringLiteral("canResolve")).toBool(),
           "screen should offer confirmation when only the matching source remains");
    recoveryController.resolveRecoveryCase(QString::fromStdString(plan.id));
    expect(recoveryController.recoveryCases().isEmpty() && QFile::exists(source),
           "screen confirmation should clear the record without moving the source");
}

} // namespace

// 미리보기 화면 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testCommonCollisionPolicyWorkflow();
    testWatchedFilesUseNormalSortWorkflow();
    testSortConfirmationSafety();
    testEveryPreviewOutcomeIsVisible();
    testSavedEditorRulesReachMainPreview();
    testPreviewFromPendingFiles();
    testTreeRulesReachPreview();
    testChangedFileCannotBeFrozen();
    testChangedRulesCannotBeFrozen();
    testChangedSettingsCannotBeFrozen();
    testChangedFileAfterSaveFailsVerification();
    testTamperedSavedPlanFailsVerification();
    testSingleFileApproveMoveUndo();
    testMultipleFilesCanBeMoved();
    testHistoryUndoUpdatesLastMove();
    testHistoryUndoAfterRestart();
    testRecoveryCasesInController();
    if (failures == 0) {
        std::cout << "All preview-controller tests passed.\n";
        return 0;
    }
    std::cerr << failures << " preview-controller assertion(s) failed.\n";
    return 1;
}
