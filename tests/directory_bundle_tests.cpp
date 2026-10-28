#include "infrastructure/filesystem/windows_directory_mover.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include <nookbit/application/operation_progress.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>

namespace {
int failures{};
/// 검사 실패를 기록하는 함수.
void expect(bool value, const std::string& message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
/// 테스트 전용 파일에 내용을 기록하는 함수.
void write(const std::filesystem::path& path, const QByteArray& content) {
    QFile file(QString::fromStdWString(path.wstring()));
    expect(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(), "create owned fixture");
}
/// 폴더 묶음의 스캔·충돌 정책·내용 검증·이동·되돌리기를 검사하는 함수.
void testBundle() {
    QTemporaryDir from;
    QTemporaryDir to(QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/out/folder-test-XXXXXX");
    expect(from.isValid() && to.isValid(), "owned fixture directories");
    if (!from.isValid() || !to.isValid()) return;
    const auto source = std::filesystem::path(from.path().toStdWString()) / L"프로젝트.png";
    const auto target = std::filesystem::path(to.path().toStdWString()) / L"프로젝트.png";
    std::filesystem::create_directories(source / L"내용/빈 폴더");
    write(source / L"내용/data.txt", "original bytes");
    write(std::filesystem::path((source / L"내용/data.txt").wstring() + L":Zone.Identifier"), "[ZoneTransfer]\r\nZoneId=3\r\n");
    nookbit::FilesystemScanner scanner;
    nookbit::QtPlanHasher hasher;
    const auto scanned = scanner.scan({{}, false, {}, {source}});
    expect(scanned && scanned.value().files.size() == 1 && scanned.value().files[0].fingerprint.directory,
        "a selected directory is one target, including its children");
    const auto fact = nookbit::scanFingerprint(scanner, source, &hasher);
    if (!fact) { expect(false, "folder digest: " + fact.error().message); return; }
    expect(!nookbit::WindowsDirectoryMover::move(source, source / "child", fact.value(), scanner, hasher),
        "cannot move folder into itself");
    nookbit::RuleSet rules; rules.schemaVersion = 3; rules.version = "folder-test";
    nookbit::RuleNode node; node.id = "by-name"; node.atomicCondition = nookbit::NameContainsPredicate{"프로젝트"};
    node.destination = nookbit::RuleDestination{L"bundles", nookbit::CollisionPolicy::Rename}; rules.nodes.push_back(node);
    nookbit::RuleEngine engine;
    nookbit::UserSettings settings; settings.homeRoot = target.parent_path();
    nookbit::Planner planner(engine, std::filesystem::path(from.path().toStdWString()) / "app-data", nookbit::CrossVolumeDrafts::Allow);
    auto preview = planner.buildPlan(scanned.value(), settings, rules);
    expect(preview && preview.value().steps.size() == 1 && preview.value().steps[0].collisionPolicy == nookbit::CollisionPolicy::Skip,
        "folder collision is skip even if its rule requests rename");
    std::filesystem::create_directories(settings.homeRoot / L"bundles" / source.filename());
    preview = planner.buildPlan(scanned.value(), settings, rules);
    expect(preview && preview.value().steps[0].status == nookbit::StepStatus::Skipped, "occupied folder is skipped as a whole");
    rules.nodes[0].atomicCondition = nookbit::ExtensionPredicate{{"png"}};
    const auto extension = engine.decide(scanned.value().files[0], rules, nookbit::MultiMatchPolicy::FirstMatch, 0);
    expect(extension && !extension.value(), "folder named .png is not an image file");
    const auto moved = nookbit::WindowsDirectoryMover::move(source, target, fact.value(), scanner, hasher);
    if (!moved) { expect(false, "bundle copy: " + moved.error().code + ": " + moved.error().message); return; }
    expect(!std::filesystem::exists(source) && std::filesystem::is_directory(target / L"내용/빈 폴더"),
        "copy commits the complete tree and preserves empty directories");
    expect(nookbit::probeFile(scanner, target, fact.value(), &hasher) == nookbit::FileProbe::Matches,
        "new directory object still matches approved contents");
    const auto current = nookbit::scanFingerprint(scanner, target, &hasher);
    const auto reversed = nookbit::WindowsDirectoryMover::move(target, source, current.value(), scanner, hasher);
    expect(reversed.hasValue(), reversed ? "reverse complete" : "reverse: " + reversed.error().message);
    expect(std::filesystem::exists(source / L"내용/data.txt") && !std::filesystem::exists(target), "reverse restores folder as one bundle");
    std::filesystem::create_directory(target);
    const auto collision = nookbit::WindowsDirectoryMover::move(source, target, fact.value(), scanner, hasher);
    expect(!collision && std::filesystem::exists(source / L"내용/data.txt"), "collision preserves source and destination");
}

/// 시험용 분류 규칙을 제공하는 저장소.
struct Rules final : nookbit::IRuleSetRepository {
    nookbit::RuleSet value;
    /// 고정된 이름 조건을 구성하는 생성자.
    Rules() {
        value.schemaVersion = 3; value.id = "folders"; value.version = "1";
        nookbit::RuleNode node; node.id = "by-name"; node.atomicCondition = nookbit::NameContainsPredicate{"bundle"};
        node.destination = nookbit::RuleDestination{L"bundles", nookbit::CollisionPolicy::Rename}; value.nodes.push_back(node);
    }
    /// 시험용 규칙을 반환하는 함수.
    nookbit::Result<nookbit::RuleSet> load() const override { return nookbit::Result<nookbit::RuleSet>::success(value); }
};
struct Settings final : nookbit::ISettingsRepository {
    nookbit::UserSettings value;
    /// 시험용 설정을 반환하는 함수.
    nookbit::Result<nookbit::UserSettings> load() const override { return nookbit::Result<nookbit::UserSettings>::success(value); }
    /// 시험용 설정을 갱신하는 함수.
    nookbit::Result<void> save(const nookbit::UserSettings& settings) override { value = settings; return nookbit::Result<void>::success(); }
};
/// 기존 승인·SQLite 기록·실행·되돌리기 흐름이 폴더에도 그대로 적용되는지 검사하는 함수.
void testApprovedWorkflow(bool sameVolume, bool lateCollision) {
    QTemporaryDir from;
    QTemporaryDir to(QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/out/folder-workflow-XXXXXX");
    if (!from.isValid() || !to.isValid()) { expect(false, "workflow fixtures"); return; }
    const auto base = std::filesystem::path(from.path().toStdWString());
    const auto appData = base / "app-data";
    Settings settings; settings.value.homeRoot = sameVolume ? base / "organized" : std::filesystem::path(to.path().toStdWString());
    Rules rules; nookbit::FilesystemScanner scanner; nookbit::QtPlanHasher hasher;
    nookbit::RuleEngine engine; nookbit::Planner planner(engine, appData, nookbit::CrossVolumeDrafts::Allow);
    nookbit::WindowsStagedCopy volumes;
    nookbit::SqliteHistoryRepository history(from.filePath("history.sqlite"),
        QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
    std::vector<std::filesystem::path> selected{base / "bundle-one", base / "bundle-two"};
    for (const auto& path : selected) { std::filesystem::create_directories(path / "empty"); write(path / "data.txt", "workflow"); }
    nookbit::CreatePreviewUseCase preview(rules, scanner, planner, &hasher, &volumes);
    auto draft = preview.execute({{}, selected, settings.value.homeRoot});
    if (!draft) { expect(false, "workflow preview: " + draft.error().message); return; }
    if (!sameVolume) expect(draft.value().steps[0].movement->strategy == nookbit::MoveStrategy::VerifiedCopy,
        "the C-to-D fixture really uses different local volumes");
    if (sameVolume) for (auto& step : draft.value().steps) step.movement->strategy = nookbit::MoveStrategy::MetadataRename;
    nookbit::FreezePreviewUseCase freeze(rules, scanner, planner, hasher, history, &volumes);
    const auto frozen = freeze.execute(draft.value(), selected, settings.value);
    if (!frozen) { expect(false, "workflow freeze: " + frozen.error().message); return; }
    nookbit::VerifySavedPlanUseCase verify(rules, scanner, planner, hasher, history, &volumes);
    nookbit::ApprovePlanUseCase approve(verify, history);
    const auto approval = approve.execute(frozen.value().id, selected, settings.value);
    if (!approval) { expect(false, "workflow approval: " + approval.error().message); return; }
    auto approved = history.loadPlan(frozen.value().id);
    expect(approved && approved.value().steps[0].fingerprint.directory, "directory type survives history round-trip");
    if (!approved) return;
    auto altered = approved.value(); altered.steps[0].fingerprint.directory = false;
    expect(nookbit::PlanSignature::approvalPayload(altered) != nookbit::PlanSignature::approvalPayload(approved.value()), "directory type is signed");
    if (lateCollision) { std::filesystem::create_directories(approved.value().steps[0].destination); write(approved.value().steps[0].destination / "occupant.txt", "occupant"); }
    nookbit::SafeExecutor executor(settings, verify, scanner, hasher, history, appData, &volumes);
    const auto result = executor.execute(approved.value(), approval.value(), selected);
    expect(bool(result) != lateCollision, result ? "workflow succeeds" : "workflow result: " + result.error().code + ": " + result.error().message);
    const auto after = history.loadPlan(frozen.value().id);
    expect(after && after.value().steps[1].status == nookbit::StepStatus::Applied, "second folder still moves");
    if (!after) return;
    expect(after.value().status == (lateCollision ? nookbit::PlanStatus::Failed : nookbit::PlanStatus::Completed), "folder plan final state");
    const auto undone = executor.rollback(after.value().id);
    expect(undone.hasValue(), undone ? "undo success" : "workflow undo: " + undone.error().code + ": " + undone.error().message);
    expect(std::filesystem::exists(selected[0] / "data.txt") && std::filesystem::exists(selected[1] / "data.txt"), "undo preserves original folder trees");
}

/// 승인 뒤 내용 변경과 제거 직전 새 파일 추가에도 사용자 데이터가 사라지지 않는지 검사하는 함수.
void testConcurrentChanges(bool addBeforeRemoval) {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "concurrent fixture"); return; }
    const auto base = std::filesystem::path(directory.path().toStdWString());
    const auto source = base / "source", destination = base / "target";
    std::filesystem::create_directory(source);
    write(source / "original.txt", "original data");
    nookbit::FilesystemScanner scanner; nookbit::QtPlanHasher hasher;
    const auto original = nookbit::scanFingerprint(scanner, source, &hasher);
    if (!original) { expect(false, "concurrent fingerprint"); return; }
    bool changed = false;
    nookbit::OperationProgressScope progress([&](const nookbit::OperationProgress& update) {
        const auto phase = addBeforeRemoval ? nookbit::OperationPhase::RemovingSource : nookbit::OperationPhase::Copying;
        if (changed || update.phase != phase) return;
        changed = true;
        write(source / (addBeforeRemoval ? "new-user-file.txt" : "original.txt"), "new user data");
    });
    const auto result = nookbit::WindowsDirectoryMover::move(source, destination, original.value(), scanner, hasher);
    expect(changed && !result, "concurrent mutation causes a conservative failure");
    if (addBeforeRemoval) {
        expect(result.error().code == "folder.cleanup_needs_review" && std::filesystem::exists(source / "new-user-file.txt") &&
            std::filesystem::exists(destination / "original.txt"), "unlisted file survives; complete destination remains available");
    } else {
        QFile retained(QString::fromStdWString((source / "original.txt").wstring()));
        expect(retained.open(QIODevice::ReadOnly) && retained.readAll() == "new user data" && !std::filesystem::exists(destination),
            "changed source is not deleted or committed as approved content");
    }
}
}
/// 폴더 전송 시험을 실제 Windows 파일 시스템에서 실행하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testBundle();
    testApprovedWorkflow(true, false);
    testApprovedWorkflow(true, true);
    testApprovedWorkflow(false, false);
    testApprovedWorkflow(false, true);
    testConcurrentChanges(false);
    testConcurrentChanges(true);
    return failures == 0 ? 0 : 1;
}
