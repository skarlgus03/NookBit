#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"
#include "presentation/pending_files_model.hpp"
#include "presentation/folder_flow_graph.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>

namespace {
int failures = 0;
// 실패한 기능 계약을 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 시험 소유 파일만 생성하는 함수.
void write(const std::filesystem::path& path) {
    QFile file(QString::fromStdWString(path.wstring()));
    expect(file.open(QIODevice::WriteOnly) && file.write("folder-flow-test") > 0, "create fixture");
}
struct Settings final : nookbit::ISettingsRepository {
    nookbit::UserSettings value;
    // 시험용 설정만 반환하는 함수.
    nookbit::Result<nookbit::UserSettings> load() const override { return nookbit::Result<nookbit::UserSettings>::success(value); }
    // 시험용 설정을 교체하는 함수.
    nookbit::Result<void> save(const nookbit::UserSettings& next) override { value = next; return nookbit::Result<void>::success(); }
};

// 실제 폴더를 펼친 뒤 기존 승인·이동·되돌리기가 원래 하위 경로를 복구하는지 검사하는 함수.
void workflow(bool custom = false) {
    QTemporaryDir temp;
    expect(temp.isValid(), "temporary workspace");
    if (!temp.isValid()) return;
    const auto base = std::filesystem::path(temp.path().toStdWString());
    const auto folder = base / "input";
    std::filesystem::create_directories(folder / "nested/empty");
    write(folder / "one.txt"); write(folder / "nested/two.png");
    nookbit::RuleSet rules;
    rules.id = "flow-test"; rules.version = "2026-11-04T00:00:00Z"; rules.schemaVersion = 3;
    nookbit::RuleNode node; node.id = "all"; node.otherwise = true;
    node.destination = nookbit::RuleDestination{L"Sorted", {}, false};
    rules.nodes.push_back(node);
    rules.folderFlows.push_back({"contents", "내용물 정리"});
    if (custom) {
        rules.folderFlows[0].entry = "input";
        rules.folderFlows[0].nodes = {{"input", "input", {"scan"}}, {"scan", "traverse", {"call-images"}}, {"call-images", "call"}};
        rules.folderFlows[0].nodes.back().targetFlow = "images";
        nookbit::RuleSet::FolderFlow images{"images", "이미지 별도 분류"};
        images.folderInput = false; images.entry = "input";
        images.nodes = {{"input", "input", {"png"}}, {"png", "condition", {"destination"}}, {"destination", "destination"}};
        images.nodes[1].condition = nookbit::ExtensionPredicate{{"png"}};
        images.nodes[2].destination = nookbit::RuleDestination{L"CustomImages", {}, false};
        rules.folderFlows.push_back(images);
    }
    nookbit::JsonRuleSetRepository repository(temp.filePath("rules.json"), QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/schemas/rules-tree-v3.schema.json");
    expect(bool(repository.save(rules)), "save named flows");
    const auto loaded = repository.load();
    expect(loaded && loaded.value().folderFlows.size() == (custom ? 2 : 1) && loaded.value().folderFlows[0].name == "내용물 정리", "JSON preserves flow identity and name");
    auto changed = rules; changed.folderFlows[0].name = "이름 변경";
    expect(nookbit::PlanSignature::rules(rules) != nookbit::PlanSignature::rules(changed), "flow changes invalidate rule revision");
    auto invalid = rules; invalid.folderFlows.push_back({"other", "내용물 정리"});
    expect(!repository.save(invalid), "duplicate display names rejected atomically");

    nookbit::FilesystemScanner scanner; nookbit::RuleEngine engine; nookbit::QtPlanHasher hasher;
    Settings settings; settings.value.homeRoot = base / "organized";
    nookbit::Planner planner(engine, base / "app-data", nookbit::CrossVolumeDrafts::Allow);
    nookbit::WindowsStagedCopy volumes;
    nookbit::CreatePreviewUseCase preview(repository, scanner, planner, &hasher, &volumes, &engine);
    nookbit::CreatePreviewRequest request{{}, {folder}, settings.value.homeRoot};
    const auto bundle = preview.execute(request);
    expect(bundle && bundle.value().steps.size() == 1 && bundle.value().steps[0].fingerprint.directory, "unselected folder remains one bundle");
    request.folderSelections = {{folder, "contents", rules.id}};
    const auto draft = preview.execute(request);
    expect(draft && draft.value().steps.size() == (custom ? 1 : 2), "selected flow interprets its own nodes instead of always applying base rules");
    if (!draft) { std::cerr << draft.error().message << '\n'; return; }
    std::vector<std::filesystem::path> selected;
    for (const auto& outcome : draft.value().previewOutcomes) selected.push_back(outcome.source);
    expect(selected.size() == 2 && std::filesystem::exists(folder / "nested/two.png"), "preview is read only and captures actual files");
    write(folder / "added-after-preview.txt");
    nookbit::SqliteHistoryRepository history(temp.filePath("history.sqlite"), QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
    nookbit::FreezePreviewUseCase freeze(repository, scanner, planner, hasher, history, &volumes);
    const auto frozen = freeze.execute(draft.value(), selected, settings.value);
    expect(bool(frozen), "freeze rescans the captured file set");
    if (!frozen) { std::cerr << frozen.error().message << '\n'; return; }
    nookbit::VerifySavedPlanUseCase verify(repository, scanner, planner, hasher, history, &volumes);
    nookbit::ApprovePlanUseCase approve(verify, history);
    const auto approval = approve.execute(frozen.value().id, selected, settings.value);
    expect(bool(approval), "approve individual-file plan");
    if (!approval) return;
    const auto approved = history.loadPlan(frozen.value().id);
    if (!approved) { expect(false, "load approved plan"); return; }
    expect(approved.value().processingSources.size() == selected.size(), "SQLite retains signed flow context for classified and unmatched files");
    auto tampered = approved.value(); tampered.processingSources[0].flowId = "different-flow";
    expect(nookbit::PlanSignature::approvalPayload(tampered) != nookbit::PlanSignature::approvalPayload(approved.value()), "changing flow context changes the approval signature");
    nookbit::SafeExecutor executor(settings, verify, scanner, hasher, history, base / "app-data", &volumes);
    const auto moved = executor.execute(approved.value(), approval.value(), selected);
    expect(bool(moved), "execute flow through existing mover");
    if (!moved) std::cerr << moved.error().message << '\n';
    expect(std::filesystem::exists(folder / "added-after-preview.txt") && std::filesystem::is_directory(folder / "nested/empty"), "new files and source folders stay untouched");
    expect(std::filesystem::exists(settings.value.homeRoot / (custom ? "CustomImages/two.png" : "Sorted/two.png")), "nested file follows chosen classification algorithm");
    if (custom) expect(std::filesystem::exists(folder / "one.txt"), "custom flow leaves unmatched file in place");
    expect(bool(executor.rollback(frozen.value().id)), "undo flow as one operation");
    expect(std::filesystem::exists(folder / "one.txt") && std::filesystem::exists(folder / "nested/two.png"), "undo restores original nested source paths");
    request.folderSelections[0].folderFlowId = "missing";
    expect(!preview.execute(request), "unknown flow cannot silently expand a folder");
    request.folderSelections[0] = {folder, "contents", "different-rules"};
    expect(!preview.execute(request), "flow selection is bound to its rule set");
    expect(!scanner.scan({{}, false, {}, {folder / "one.txt"}, {folder / "one.txt"}}), "file cannot use folder flow");
    expect(!scanner.scan({{}, false, {}, {folder}, {base / "other"}}), "unselected folders cannot be expanded");

    nookbit::PendingFilesModel pending;
    pending.setFolderFlows("flow-test", rules.folderFlows);
    expect(bool(pending.addPath(folder)), "queue folder as one row");
    pending.setFolderFlow(0, "contents");
    expect(pending.count() == 1 && pending.entries()[0].folderFlowId == "contents", "flow selection never unfolds queue rows");
    nookbit::JsonPendingFilesRepository storage(temp.filePath("pending.json"));
    expect(bool(storage.saveEntries(pending.entries())), "persist folder selection");
    const auto restored = storage.loadEntries();
    nookbit::PendingFilesModel next; next.setFolderFlows("flow-test", rules.folderFlows);
    if (restored) next.restoreEntries(restored.value());
    expect(next.count() == 1 && next.entries()[0].folderFlowId == "contents", "restore scoped folder selection");
    next.setFolderFlows("new-rules", rules.folderFlows);
    expect(next.entries()[0].folderFlowId.empty() && next.lastMessage().contains("되돌렸"), "rule replacement resets and announces missing selection");
}

// 편집기의 네모 흐름이 일반 코멘트가 아니라 검증되는 실행 입구인지 검사하는 함수.
void graphContract() {
    QVariantList blocks{QVariantMap{{"id", 1}, {"type", "start"}}};
    QVariantList edges;
    int next = 2;
    nookbit::appendFolderFlowGraph({{"contents", "내용물 정리"}}, {}, blocks, edges, next, 200);
    auto split = nookbit::splitFolderFlowGraph(blocks, edges);
    expect(split && split.value().flows.size() == 1 && split.value().blocks.size() == 1 && split.value().edges.empty(), "flow boundary splits from base classification");
    auto malformed = edges; malformed.append(QVariantMap{{"from", 1}, {"to", 2}});
    expect(!nookbit::splitFolderFlowGraph(blocks, malformed), "cross-flow links rejected");
    malformed = edges; malformed[1] = QVariantMap{{"from", 3}, {"to", 2}};
    expect(!nookbit::splitFolderFlowGraph(blocks, malformed), "cyclic folder flow rejected");
    blocks.removeLast();
    expect(!nookbit::splitFolderFlowGraph(blocks, edges), "incomplete folder flow rejected");
    nookbit::RuleSet rules;
    auto first = nookbit::expandedFlow({"a", "A"});
    auto second = nookbit::expandedFlow({"b", "B"});
    first.nodes.back().targetFlow = "b";
    rules.folderFlows = {first, second};
    expect(!nookbit::validateProcessingFlows(rules), "file output cannot call folder-only input");
    first.nodes[0].next = {"apply"}; first.nodes.erase(first.nodes.begin() + 1);
    second.nodes[0].next = {"apply"}; second.nodes.erase(second.nodes.begin() + 1);
    second.nodes.back().targetFlow = "a";
    rules.folderFlows = {first, second};
    expect(!nookbit::validateProcessingFlows(rules), "cross-flow recursion rejected even when graph edges do not cycle");
    auto condition=std::make_shared<nookbit::LogicalPredicate>();
    condition->operands={nookbit::ItemTypePredicate{nookbit::ItemType::Folder},nookbit::NameContainsPredicate{"Project"}};
    nookbit::RuleSet::FolderFlow guarded;guarded.id="guarded";guarded.name="Guarded";guarded.entry="input";guarded.folderInput=false;
    guarded.nodes={{"input","input",{"guard"}},{"guard","condition",{"scan"},nookbit::LogicalPredicatePtr(condition)},
        {"scan","traverse",{"apply"}},{"apply","call"}};
    rules.folderFlows={guarded};
    expect(nookbit::validateProcessingFlows(rules).hasValue(),"AND folder guard safely narrows traversal input");
    auto alternative=std::make_shared<nookbit::LogicalPredicate>(*condition);alternative->all=false;
    rules.folderFlows[0].nodes[1].condition=nookbit::LogicalPredicatePtr(alternative);
    expect(!nookbit::validateProcessingFlows(rules),"OR name match cannot allow files into a folder traversal");

}
}

// 실제 파일과 저장소를 쓰는 폴더 흐름 검증을 실행하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    workflow(); workflow(true); graphContract();
    return failures ? 1 : 0;
}
