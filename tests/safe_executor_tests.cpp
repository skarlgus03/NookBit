#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

int failures = 0;

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트 내부의 예제나 Schema 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

// 테스트에서 한 목적지 설정만 제공하는 저장소.
class TestSettingsRepository final : public nookbit::ISettingsRepository {
public:
    nookbit::UserSettings settings;

    // 현재 설정을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::UserSettings> load() const override {
        return nookbit::Result<nookbit::UserSettings>::success(settings);
    }

    // 테스트에서 설정 저장을 거절하는 함수.
    [[nodiscard]] nookbit::Result<void> save(const nookbit::UserSettings&) override {
        return nookbit::Result<void>::failure({"test.read_only", "Not needed in this test."});
    }
};

// 이동 의도 기록 도중 실패·충돌·파일 변경을 재현하는 저장소.
class FailingIntentHistory final : public nookbit::IHistoryRepository {
public:
    std::function<void()> onIntent;
    QString changedSource;
    QString changedRollbackDestination;
    // 실제 SQLite 저장소를 테스트 대리자로 받는 생성자.
    explicit FailingIntentHistory(nookbit::SqliteHistoryRepository& backing,
                                  QString collisionDestination = {})
        : backing_(backing), collisionDestination_(std::move(collisionDestination)) {}

    // 계획 저장을 실제 저장소에 위임하는 함수.
    [[nodiscard]] nookbit::Result<void> savePlan(const nookbit::OperationPlan& plan) override {
        return backing_.savePlan(plan);
    }

    // 승인을 실제 저장소에 위임하는 함수.
    [[nodiscard]] nookbit::Result<void> saveApproval(const nookbit::ApprovalRecord& approval) override {
        return backing_.saveApproval(approval);
    }

    // 이동·되돌리기 의도를 기록할 때 테스트용 오류나 파일 변경을 주입하는 함수.
    [[nodiscard]] nookbit::Result<void> appendEvent(const nookbit::DomainEvent& event) override {
        if (const auto* changed = std::get_if<nookbit::StepStatusChanged>(&event);
            changed && (changed->status == nookbit::StepStatus::IntentRecorded ||
                        changed->status == nookbit::StepStatus::RollbackIntentRecorded) && onIntent) onIntent();
        if (const auto* changed = std::get_if<nookbit::StepStatusChanged>(&event);
            changed && changed->status == nookbit::StepStatus::IntentRecorded && ++intents_ == 2) {
            if (!changedSource.isEmpty()) {
                QFile source(changedSource);
                if (!source.open(QIODevice::WriteOnly) || source.write("changed source") != 14) {
                    return nookbit::Result<void>::failure({"test.source_failed", "Cannot change the source fixture."});
                }
            } else if (collisionDestination_.isEmpty()) {
                return nookbit::Result<void>::failure({
                    "test.intent_failed", "Injected second-step journal failure.",
                });
            } else {
                QFile collision(collisionDestination_);
                if (!collision.open(QIODevice::WriteOnly) || collision.write("occupant") != 8) {
                    return nookbit::Result<void>::failure({
                        "test.collision_failed", "Cannot create second-step collision fixture.",
                    });
                }
            }
        }
        if (const auto* changed = std::get_if<nookbit::StepStatusChanged>(&event);
            changed && changed->status == nookbit::StepStatus::RollbackIntentRecorded &&
            !changedRollbackDestination.isEmpty()) {
            QFile destination(changedRollbackDestination);
            if (!destination.open(QIODevice::WriteOnly) || destination.write("changed destination") != 19) {
                return nookbit::Result<void>::failure({"test.undo_failed", "Cannot change the undo fixture."});
            }
            changedRollbackDestination.clear();
        }
        return backing_.appendEvent(event);
    }

    // 미완료 계획 조회를 실제 저장소에 위임하는 함수.
    [[nodiscard]] nookbit::Result<std::vector<nookbit::OperationPlan>> incompletePlans() const override {
        return backing_.incompletePlans();
    }

    // 계획 조회를 실제 저장소에 위임하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::OperationPlan> loadPlan(const nookbit::PlanId& id) const override {
        return backing_.loadPlan(id);
    }

    // 최근 작업 목록 조회를 실제 저장소에 위임하는 함수.
    [[nodiscard]] nookbit::Result<std::vector<nookbit::PlanHistoryEntry>> recentPlans(int limit) const override {
        return backing_.recentPlans(limit);
    }

    // 중단 전 상태 조회를 실제 저장소에 위임하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::PlanStatus> interruptedStatus(const nookbit::PlanId& id) const override {
        return backing_.interruptedStatus(id);
    }

private:
    nookbit::SqliteHistoryRepository& backing_;
    QString collisionDestination_;
    int intents_{0};
};

// 임시 파일을 만들고 원본 내용을 기록하는 함수.
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(bytes) == bytes.size();
}

// 임시 파일의 내용을 읽는 함수.
QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// 명시적 승인 없이는 이동하지 않고 승인 후 안전하게 되돌리는지 검사하는 함수.
void testApprovedSingleFileMoveAndUndo() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;
    const auto incoming = directory.filePath(QStringLiteral("incoming"));
    expect(QDir().mkpath(incoming), "incoming directory should be created");
    const auto source = QDir(incoming).filePath(QStringLiteral("photo.jpg"));
    const QByteArray original("original image bytes");
    expect(writeFile(source, original), "original file should be written");
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = directory.filePath(QStringLiteral("app-data"));
    const auto dbPath = QDir(appData).filePath(QStringLiteral("history.sqlite"));
    const auto sourcePath = std::filesystem::path(source.toStdWString());

    TestSettingsRepository settings;
    settings.settings.homeRoot = std::filesystem::path(home.toStdWString());
    nookbit::JsonRuleSetRepository rules(
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine ruleEngine;
    nookbit::Planner planner(ruleEngine, std::filesystem::path(appData.toStdWString()));
    nookbit::QtPlanHasher hasher;
    nookbit::SqliteHistoryRepository history(
        dbPath, projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher);
    const auto loadedRules = rules.load();
    expect(loadedRules.hasValue(), "default rules should load");
    if (!loadedRules) return;
    const auto draft = preview.execute({loadedRules.value().version, {sourcePath}});
    expect(draft.hasValue(), "draft should be created");
    if (!draft) return;
    nookbit::FreezePreviewUseCase freezer(rules, scanner, planner, hasher, history);
    const auto frozen = freezer.execute(draft.value(), {sourcePath}, settings.settings);
    expect(frozen.hasValue(), "draft should be frozen");
    if (!frozen) return;
    expect(frozen.value().steps.front().fingerprint.sha256.has_value(),
           "frozen plan should store the file content hash");
    const auto destination = QString::fromStdWString(frozen.value().steps.front().destination.wstring());
    expect(QFile::exists(source) && !QFile::exists(destination),
           "frozen plan should not change files");

    nookbit::VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history);
    nookbit::SafeExecutor executor(
        settings, verifier, scanner, hasher, history,
        std::filesystem::path(appData.toStdWString()));
    const nookbit::ApprovalRecord fakeApproval{
        frozen.value().id, frozen.value().planHash, std::chrono::system_clock::now()};
    const auto refused = executor.execute(frozen.value(), fakeApproval, {sourcePath});
    expect(!refused.hasValue(), "unapproved plan must not execute");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "rejected execution must not change files");

    nookbit::ApprovePlanUseCase approver(verifier, history);
    const auto approval = approver.execute(
        frozen.value().id, {sourcePath}, settings.settings);
    expect(approval.hasValue(), "unchanged plan should receive explicit approval");
    if (!approval) return;
    const auto approved = history.loadPlan(frozen.value().id);
    expect(approved.hasValue() && approved.value().status == nookbit::PlanStatus::Approved,
           "approved status should be persisted");
    if (!approved) return;
    nookbit::SafeExecutor rollbackOnly(
        settings, scanner, hasher, history, std::filesystem::path(appData.toStdWString()));
    const auto missingVerifier = rollbackOnly.execute(
        approved.value(), approval.value(), {sourcePath});
    expect(!missingVerifier.hasValue() &&
               missingVerifier.error().code == "execute.verifier_missing" &&
               QFile::exists(source) && !QFile::exists(destination),
           "rollback-only executor must reject a move without changing files");
    auto wrongApproval = approval.value();
    wrongApproval.planHash = std::string(64, '0');
    const auto wrongHash = executor.execute(approved.value(), wrongApproval, {sourcePath});
    expect(!wrongHash.hasValue(), "wrong approval hash must not execute");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "wrong approval hash must leave files unchanged");

    const auto originalModified = QFileInfo(source).lastModified();
    const QByteArray changed(original.size(), 'x');
    expect(writeFile(source, changed), "same-size tampered source should be written");
    QFile sourceFile(source);
    expect(sourceFile.open(QIODevice::ReadWrite), "tampered source should reopen");
    if (sourceFile.isOpen()) {
        expect(sourceFile.setFileTime(originalModified, QFileDevice::FileModificationTime),
               "source modification time should be restored");
        sourceFile.close();
    }
    const auto tamperedSnapshot = scanner.scan({{}, false, {}, {sourcePath}});
    expect(tamperedSnapshot.hasValue() && tamperedSnapshot.value().files.size() == 1 &&
               tamperedSnapshot.value().files.front().fingerprint.size ==
                   frozen.value().steps.front().fingerprint.size &&
               tamperedSnapshot.value().files.front().fingerprint.modifiedUtcMs ==
                   frozen.value().steps.front().fingerprint.modifiedUtcMs &&
               tamperedSnapshot.value().files.front().fingerprint.fileId ==
                   frozen.value().steps.front().fingerprint.fileId,
           "tampered fixture should retain size, modified time and file ID");
    const auto changedContent = executor.execute(approved.value(), approval.value(), {sourcePath});
    expect(!changedContent.hasValue() && changedContent.error().code == "verify.plan_stale",
           "same-size and same-time content change must be rejected by its hash");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "content-hash rejection must not move the file");
    expect(writeFile(source, original), "original test content should be restored");
    expect(sourceFile.open(QIODevice::ReadWrite), "restored source should reopen");
    if (sourceFile.isOpen()) {
        expect(sourceFile.setFileTime(originalModified, QFileDevice::FileModificationTime),
               "original source modification time should be restored");
        sourceFile.close();
    }

    expect(QDir().mkpath(QFileInfo(destination).absolutePath()),
           "collision parent should be available");
    expect(writeFile(destination, QByteArray("preexisting file")),
           "collision fixture should be written");
    const auto collision = executor.execute(approved.value(), approval.value(), {sourcePath});
    expect(!collision.hasValue(), "destination appearing after approval must block execution");
    expect(readFile(destination) == QByteArray("preexisting file") && QFile::exists(source),
           "collision must not overwrite either file");
    expect(QFile::remove(destination), "test collision fixture should be removed");

    const auto moved = executor.execute(approved.value(), approval.value(), {sourcePath});
    expect(moved.hasValue(), "approved single-file plan should execute");
    expect(!QFile::exists(source) && QFile::exists(destination),
           "execution should move exactly the selected file");
    expect(readFile(destination) == original, "moved file contents should be preserved");
    const auto completed = history.loadPlan(frozen.value().id);
    expect(completed.hasValue() && completed.value().status == nookbit::PlanStatus::Completed,
           "completion should be recorded");

    expect(writeFile(source, QByteArray("new occupant")),
           "undo collision fixture should be written");
    const auto blockedUndo = executor.rollback(frozen.value().id);
    expect(!blockedUndo.hasValue(), "undo must refuse an occupied original path");
    expect(readFile(source) == QByteArray("new occupant") && readFile(destination) == original,
           "blocked undo must not overwrite either file");
    expect(QFile::remove(source), "test undo collision fixture should be removed");

    FailingIntentHistory changingUndoHistory(history);
    changingUndoHistory.changedRollbackDestination = destination;
    nookbit::SafeExecutor changingUndo(settings, scanner, hasher, changingUndoHistory,
                                      std::filesystem::path(appData.toStdWString()));
    const auto changedUndo = changingUndo.rollback(frozen.value().id);
    expect(!changedUndo && changedUndo.error().code == "rollback.needs_recovery" &&
               !QFile::exists(source) && readFile(destination) == QByteArray("changed destination"),
           "destination changed during undo intent must stay put without restoring unapproved bytes");
    const auto interruptedUndo = history.loadPlan(frozen.value().id);
    expect(interruptedUndo && interruptedUndo.value().status == nookbit::PlanStatus::NeedsRecovery,
           "interrupted undo must require review");
    expect(writeFile(destination, original), "undo fixture original bytes should be restored manually");
    QFile destinationFile(destination);
    expect(destinationFile.open(QIODevice::ReadWrite), "undo fixture should reopen for timestamp restoration");
    if (destinationFile.isOpen()) {
        expect(destinationFile.setFileTime(originalModified, QFileDevice::FileModificationTime),
               "undo fixture timestamp should be restored manually");
        destinationFile.close();
    }
    nookbit::RecoveryService recovery(history, scanner, hasher);
    expect(recovery.resolveCase(frozen.value().id).hasValue(),
           "manually restored destination should allow record-only recovery");

    const auto undone = executor.rollback(frozen.value().id);
    expect(undone.hasValue(), "completed move should be undoable");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "undo should return file to the original path");
    expect(readFile(source) == original, "undo should preserve file contents");
    const auto rolledBack = history.loadPlan(frozen.value().id);
    expect(rolledBack.hasValue() && rolledBack.value().status == nookbit::PlanStatus::RolledBack,
           "undo completion should be recorded");
}

// 실제 조회 앞에서 볼륨 교체 또는 다른 목적지 볼륨을 재현하는 테스트 일꾼.
class ChangingVolumes final : public nookbit::ITransferVolumeInspector {
public:
    nookbit::WindowsStagedCopy native;
    bool changed{false};
    bool differentDestination{false};
    // 실제 NTFS 조회 결과 중 GUID만 바꾸어 계약 변경을 주입하는 함수.
    nookbit::Result<nookbit::TransferVolume> inspectVolume(const std::filesystem::path& path) override {
        auto result = native.inspectVolume(path);
        if (result && (changed || (differentDestination && path.wstring().find(L"organized") != std::wstring::npos)))
            result.value().name = L"\\\\?\\Volume{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}\\";
        return result;
    }
};

// 실제 임시 파일과 SQLite로 v2 승인부터 이동·되돌리기 및 직전 볼륨 변경 거부를 검사하는 함수.
void testV2Execution(int scenario) {
    QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/v2-execute-XXXXXX");
    expect(directory.isValid(), "v2 execution fixture should exist");
    if (!directory.isValid()) return;
    const auto source = directory.filePath("photo.jpg");
    expect(writeFile(source, "v2 original contents"), "v2 source should exist");
    const std::filesystem::path sourcePath(source.toStdWString());
    const std::filesystem::path appData(directory.filePath("app-data").toStdWString());
    TestSettingsRepository settings;
    settings.settings.homeRoot = std::filesystem::path(directory.filePath("organized").toStdWString());
    nookbit::JsonRuleSetRepository rules(projectPath("resources/rules/default.example.json"), projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, appData);
    nookbit::QtPlanHasher hasher;
    nookbit::SqliteHistoryRepository history(directory.filePath("history.sqlite"), projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    ChangingVolumes volumes;
    volumes.differentDestination = scenario == 3;
    nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher, &volumes);
    const auto loadedRules = rules.load();
    if (!loadedRules) { expect(false, "v2 rules should load"); return; }
    const auto draft = preview.execute({loadedRules.value().version, {sourcePath}});
    expect(draft && draft.value().formatVersion == 2, "preview should explicitly prepare v2");
    if (!draft) return;
    nookbit::FreezePreviewUseCase missingFreezer(rules, scanner, planner, hasher, history);
    expect(!missingFreezer.execute(draft.value(), {sourcePath}, settings.settings), "v2 freeze must require a volume port");
    nookbit::FreezePreviewUseCase freezer(rules, scanner, planner, hasher, history, &volumes);
    volumes.changed = true;
    expect(!freezer.execute(draft.value(), {sourcePath}, settings.settings), "changed volume must block freezing");
    volumes.changed = false;
    const auto frozen = freezer.execute(draft.value(), {sourcePath}, settings.settings);
    expect(frozen.hasValue(), "v2 draft should freeze without changing its contract");
    if (!frozen) return;
    nookbit::VerifySavedPlanUseCase missingVerifier(rules, scanner, planner, hasher, history);
    expect(!missingVerifier.execute(frozen.value().id, {sourcePath}, settings.settings), "v2 approval must require a volume port");
    nookbit::VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history, &volumes);
    nookbit::ApprovePlanUseCase approver(verifier, history);
    volumes.changed = true;
    expect(!approver.execute(frozen.value().id, {sourcePath}, settings.settings), "volume change must block approval");
    volumes.changed = false;
    const auto approval = approver.execute(frozen.value().id, {sourcePath}, settings.settings);
    expect(approval.hasValue(), "v2 unchanged plan should approve");
    const auto approved = history.loadPlan(frozen.value().id);
    if (!approval || !approved) return;
    const auto destination = QString::fromStdWString(approved.value().steps[0].destination.wstring());
    FailingIntentHistory changingHistory(history);
    nookbit::SafeExecutor executor(settings, verifier, scanner, hasher, changingHistory, appData, &volumes);
    nookbit::SafeExecutor missingExecutor(settings, verifier, scanner, hasher, history, appData);
    expect(!missingExecutor.execute(approved.value(), approval.value(), {sourcePath}), "v2 execution must require a volume port");
    volumes.changed = true;
    expect(!executor.execute(approved.value(), approval.value(), {sourcePath}), "volume change must block execution");
    volumes.changed = false;
    if (scenario == 1) changingHistory.onIntent = [&] { volumes.changed = true; };
    const auto moved = executor.execute(approved.value(), approval.value(), {sourcePath});
    if (scenario == 1 || scenario == 3) {
        expect(!moved && QFile::exists(source) && !QFile::exists(destination), "late volume change or copy strategy must not rename source");
        const auto saved = history.loadPlan(frozen.value().id);
        expect(saved && saved.value().status == (scenario == 1 ? nookbit::PlanStatus::NeedsRecovery : nookbit::PlanStatus::Approved),
               "uncertain intent must need review, unsupported strategy must stay approved");
        if (scenario == 3) expect(moved.error().code == "execute.transfer_executor_required", "copy contract must explicitly require transfer executor");
        return;
    }
    expect(moved && !QFile::exists(source) && readFile(destination) == "v2 original contents", "v2 rename should move only approved content");
    if (!moved) return;
    volumes.changed = true;
    expect(!executor.rollback(frozen.value().id) && !QFile::exists(source) && QFile::exists(destination), "volume change must block undo before mutation");
    volumes.changed = false;
    if (scenario == 2) changingHistory.onIntent = [&] { volumes.changed = true; };
    const auto undone = executor.rollback(frozen.value().id);
    const auto saved = history.loadPlan(frozen.value().id);
    if (scenario == 2) {
        expect(!undone && !QFile::exists(source) && QFile::exists(destination) && saved && saved.value().status == nookbit::PlanStatus::NeedsRecovery,
               "late undo volume change must preserve destination and require review");
    } else {
        expect(undone && readFile(source) == "v2 original contents" && !QFile::exists(destination) && saved &&
               saved.value().status == nookbit::PlanStatus::RolledBack && saved.value().planHash == frozen.value().planHash,
               "v2 undo must restore original content without changing approval hash");
    }
}

// 두 번째 단계의 기록 실패나 목적지 충돌 뒤 첫 이동을 되돌리는지 검사하는 함수.
void testBatchFailureRollsBackEarlierMove(bool destinationCollision, bool sourceChanged = false, bool v2 = false) {
    QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/batch-execute-XXXXXX");
    expect(directory.isValid(), "batch-failure directory should be available");
    if (!directory.isValid()) return;
    const auto incoming = directory.filePath(QStringLiteral("incoming"));
    expect(QDir().mkpath(incoming), "batch-failure incoming directory should exist");
    const auto first = QDir(incoming).filePath(QStringLiteral("first.jpg"));
    const auto second = QDir(incoming).filePath(QStringLiteral("second.jpg"));
    expect(writeFile(first, QByteArray("first bytes")) &&
               writeFile(second, QByteArray("second bytes")),
           "batch-failure source files should be written");
    const auto firstPath = std::filesystem::path(first.toStdWString());
    const auto secondPath = std::filesystem::path(second.toStdWString());
    const std::vector<std::filesystem::path> selected{firstPath, secondPath};
    const auto home = directory.filePath(QStringLiteral("organized"));
    const auto appData = directory.filePath(QStringLiteral("app-data"));
    TestSettingsRepository settings;
    settings.settings.homeRoot = std::filesystem::path(home.toStdWString());
    nookbit::JsonRuleSetRepository rules(
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"));
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine ruleEngine;
    nookbit::Planner planner(ruleEngine, std::filesystem::path(appData.toStdWString()));
    nookbit::QtPlanHasher hasher;
    nookbit::SqliteHistoryRepository history(
        QDir(appData).filePath(QStringLiteral("history.sqlite")),
        projectPath("src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    auto loadedRules = rules.load();
    if (!loadedRules) {
        expect(false, "batch-failure rules should load");
        return;
    }
    nookbit::WindowsStagedCopy volumes;
    nookbit::CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher, v2 ? &volumes : nullptr);
    auto draft = preview.execute({loadedRules.value().version, selected});
    if (!draft) {
        expect(false, "batch-failure preview should succeed");
        return;
    }
    nookbit::FreezePreviewUseCase freezer(rules, scanner, planner, hasher, history, v2 ? &volumes : nullptr);
    auto frozen = freezer.execute(draft.value(), selected, settings.settings);
    if (!frozen) {
        expect(false, "batch-failure plan should freeze");
        return;
    }
    nookbit::VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history, v2 ? &volumes : nullptr);
    nookbit::ApprovePlanUseCase approver(verifier, history);
    auto approval = approver.execute(frozen.value().id, selected, settings.settings);
    auto approved = history.loadPlan(frozen.value().id);
    if (!approval || !approved) {
        expect(false, "batch-failure plan should be approved");
        return;
    }
    const auto secondDestination = QString::fromStdWString(approved.value().steps.at(1).destination.wstring());
    FailingIntentHistory failingHistory(history,
        destinationCollision ? secondDestination : QString());
    if (sourceChanged) failingHistory.changedSource = second;
    nookbit::VerifySavedPlanUseCase executionVerifier(
        rules, scanner, planner, hasher, failingHistory, v2 ? &volumes : nullptr);
    nookbit::SafeExecutor executor(settings, executionVerifier, scanner, hasher, failingHistory,
                                   std::filesystem::path(appData.toStdWString()), v2 ? &volumes : nullptr);
    auto executed = executor.execute(approved.value(), approval.value(), selected);
    expect(!executed.hasValue() && executed.error().code == "execute.batch_rolled_back",
           "second-step failure should report completed compensation");
    const auto firstDestination = QString::fromStdWString(approved.value().steps.at(0).destination.wstring());
    expect(QFile::exists(first) && QFile::exists(second) &&
               !QFile::exists(firstDestination) &&
               (destinationCollision ? readFile(secondDestination) == QByteArray("occupant")
                                     : !QFile::exists(secondDestination)),
           "batch compensation should restore both original paths");
    expect(readFile(first) == QByteArray("first bytes") &&
               readFile(second) == QByteArray(sourceChanged ? "changed source" : "second bytes"),
           "changed source must stay at its original path without undoing external edits");
    auto saved = history.loadPlan(frozen.value().id);
    expect(saved.hasValue() && saved.value().status == nookbit::PlanStatus::Failed &&
               saved.value().steps.at(0).status == nookbit::StepStatus::RolledBack &&
               saved.value().steps.at(1).status ==
                   (destinationCollision || sourceChanged ? nookbit::StepStatus::Failed : nookbit::StepStatus::Planned),
           "batch journal should record rollback and unstarted step");
}

// 큰 파일을 나눠 읽는 해시가 전체 바이트 해시와 같은지 검사하는 함수.
void testStreamingContentHash() {
    QTemporaryDir directory;
    expect(directory.isValid(), "content-hash directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("large.bin"));
    const QByteArray bytes(2 * 1024 * 1024 + 17, 'x');
    expect(writeFile(path, bytes), "content-hash fixture should be written");
    nookbit::QtPlanHasher hasher;
    const auto digest = hasher.sha256File(std::filesystem::path(path.toStdWString()));
    expect(digest.hasValue() && digest.value() == hasher.sha256(
               std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))),
           "streamed file hash should equal the in-memory hash");
    expect(!hasher.sha256File(std::filesystem::path(
               directory.filePath(QStringLiteral("missing.bin")).toStdWString())).hasValue(),
           "missing file must not produce a content hash");
}

} // namespace

// 안전한 단일 파일 이동·되돌리기 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testStreamingContentHash();
    testApprovedSingleFileMoveAndUndo();
    for (int scenario = 0; scenario < 4; ++scenario) testV2Execution(scenario);
    testBatchFailureRollsBackEarlierMove(false);
    testBatchFailureRollsBackEarlierMove(true);
    testBatchFailureRollsBackEarlierMove(false, true);
    testBatchFailureRollsBackEarlierMove(false, false, true);
    testBatchFailureRollsBackEarlierMove(true, false, true);
    testBatchFailureRollsBackEarlierMove(false, true, true);
    if (failures == 0) {
        std::cout << "All safe-executor tests passed.\n";
        return 0;
    }
    std::cerr << failures << " safe-executor assertion(s) failed.\n";
    return 1;
}
