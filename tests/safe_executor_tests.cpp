#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
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
#include <iostream>
#include <string>

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
    nookbit::FreezePreviewUseCase freezer(settings, rules, scanner, planner, hasher, history);
    const auto frozen = freezer.execute(draft.value(), {sourcePath});
    expect(frozen.hasValue(), "draft should be frozen");
    if (!frozen) return;
    const auto destination = QString::fromStdWString(frozen.value().steps.front().destination.wstring());
    expect(QFile::exists(source) && !QFile::exists(destination),
           "frozen plan should not change files");

    nookbit::SafeExecutor executor(
        settings, rules, scanner, planner, hasher, history,
        std::filesystem::path(appData.toStdWString()));
    const nookbit::ApprovalRecord fakeApproval{
        frozen.value().id, frozen.value().planHash, std::chrono::system_clock::now()};
    const auto refused = executor.execute(frozen.value(), fakeApproval, {sourcePath});
    expect(!refused.hasValue(), "unapproved plan must not execute");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "rejected execution must not change files");

    nookbit::VerifySavedPlanUseCase verifier(settings, rules, scanner, planner, hasher, history);
    nookbit::ApprovePlanUseCase approver(verifier, history);
    const auto approval = approver.execute(frozen.value().id, {sourcePath});
    expect(approval.hasValue(), "unchanged plan should receive explicit approval");
    if (!approval) return;
    const auto approved = history.loadPlan(frozen.value().id);
    expect(approved.hasValue() && approved.value().status == nookbit::PlanStatus::Approved,
           "approved status should be persisted");
    if (!approved) return;
    auto wrongApproval = approval.value();
    wrongApproval.planHash = std::string(64, '0');
    const auto wrongHash = executor.execute(approved.value(), wrongApproval, {sourcePath});
    expect(!wrongHash.hasValue(), "wrong approval hash must not execute");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "wrong approval hash must leave files unchanged");

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

    const auto undone = executor.rollback(frozen.value().id);
    expect(undone.hasValue(), "completed move should be undoable");
    expect(QFile::exists(source) && !QFile::exists(destination),
           "undo should return file to the original path");
    expect(readFile(source) == original, "undo should preserve file contents");
    const auto rolledBack = history.loadPlan(frozen.value().id);
    expect(rolledBack.hasValue() && rolledBack.value().status == nookbit::PlanStatus::RolledBack,
           "undo completion should be recorded");
}

} // namespace

// 안전한 단일 파일 이동·되돌리기 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testApprovedSingleFileMoveAndUndo();
    if (failures == 0) {
        std::cout << "All safe-executor tests passed.\n";
        return 0;
    }
    std::cerr << failures << " safe-executor assertion(s) failed.\n";
    return 1;
}
