#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
    nookbit::PreviewController controller(
        pending,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(appData.toStdWString()), home,
        directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    controller.createPreview(home);

    expect(controller.hasPreview(), "preview should be available");
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
    nookbit::PreviewController controller(
        pending,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home, directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
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
    nookbit::PreviewController controller(
        pending,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home, dbPath,
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
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
    nookbit::PreviewController controller(
        pending, rulesPath, projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home, directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
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
    nookbit::FreezePreviewUseCase freeze(settings, rules, scanner, planner, hasher, history);
    const auto result = freeze.execute(draft.value(), {sourcePath});
    expect(!result.hasValue() && result.error().code == "freeze.settings_changed",
           "setting changes must be rejected even when planned destination is unchanged");
    expect(!QFile::exists(directory.filePath(QStringLiteral("history.sqlite"))),
           "changed settings must not create a stored plan");
    expect(QFile::exists(source), "settings-change rejection must leave the source in place");

    settings.settings.recursiveScan = false;
    const auto frozen = freeze.execute(draft.value(), {sourcePath});
    expect(frozen.hasValue(), "restored settings should allow storing the plan");
    if (!frozen) return;
    nookbit::VerifySavedPlanUseCase verify(settings, rules, scanner, planner, hasher, history);
    expect(verify.execute(frozen.value().id, {sourcePath}).hasValue(),
           "unchanged settings and rules should verify");

    settings.settings.recursiveScan = true;
    const auto changedSettings = verify.execute(frozen.value().id, {sourcePath});
    expect(!changedSettings.hasValue() && changedSettings.error().code == "verify.settings_changed",
           "post-save setting changes should fail verification");
    settings.settings.recursiveScan = false;
    rules.rules.rules.front().destinationRelativePath = L"Other";
    const auto changedRules = verify.execute(frozen.value().id, {sourcePath});
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
    nookbit::PreviewController controller(
        pending,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home,
        directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
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
    nookbit::PreviewController controller(
        pending, projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home, directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
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

// 여러 대기 파일에 대한 승인 시도는 실제 이동으로 이어지지 않아야 한다.
void testMultipleFilesCannotBeApproved() {
    QTemporaryDir directory;
    expect(directory.isValid(), "multi-file directory should be available");
    if (!directory.isValid()) return;
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    for (const auto& path : {first, second}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "multi-file source should open");
        if (!file.isOpen()) return;
        file.write("content");
    }
    nookbit::PendingFilesModel pending;
    pending.addUrl(QUrl::fromLocalFile(first));
    pending.addUrl(QUrl::fromLocalFile(second));
    const auto home = directory.filePath(QStringLiteral("organized"));
    nookbit::PreviewController controller(
        pending, projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home, directory.filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    controller.createPreview(home);
    controller.freezePreview();
    controller.approvePlan();
    expect(!controller.hasApprovedPlan(), "multi-file plan must not be approved from the screen");
    expect(QFile::exists(first) && QFile::exists(second),
           "multi-file approval attempt must not move either file");
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
    nookbit::PreviewController controller(
        pending, projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        std::filesystem::path(directory.filePath(QStringLiteral("app-data")).toStdWString()),
        home, databasePath, migrationPath);
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
    controller.refreshRecoveryCases();
    expect(controller.recoveryCases().size() == 1 &&
               !controller.recoveryCases().front().toMap().value(QStringLiteral("canResolve")).toBool(),
           "screen should show ambiguity without offering confirmation");
    expect(QFile::remove(destination), "test copy should be removed");
    controller.refreshRecoveryCases();
    expect(controller.recoveryCases().size() == 1 &&
               controller.recoveryCases().front().toMap().value(QStringLiteral("canResolve")).toBool(),
           "screen should offer confirmation when only the matching source remains");
    controller.resolveRecoveryCase(QString::fromStdString(plan.id));
    expect(controller.recoveryCases().isEmpty() && QFile::exists(source),
           "screen confirmation should clear the record without moving the source");
}

} // namespace

// 미리보기 화면 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testPreviewFromPendingFiles();
    testChangedFileCannotBeFrozen();
    testChangedRulesCannotBeFrozen();
    testChangedSettingsCannotBeFrozen();
    testChangedFileAfterSaveFailsVerification();
    testTamperedSavedPlanFailsVerification();
    testSingleFileApproveMoveUndo();
    testMultipleFilesCannotBeApproved();
    testRecoveryCasesInController();
    if (failures == 0) {
        std::cout << "All preview-controller tests passed.\n";
        return 0;
    }
    std::cerr << failures << " preview-controller assertion(s) failed.\n";
    return 1;
}
