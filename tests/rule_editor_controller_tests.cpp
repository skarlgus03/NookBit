#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "presentation/rule_editor_controller.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;

// 검사 결과가 틀리면 테스트 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

// 프로젝트에 들어 있는 테스트 규칙 파일 경로를 반환하는 함수.
QString projectPath(const char* relative) {
    return QStringLiteral(NOOKBIT_SOURCE_DIR) + QLatin1Char('/') +
           QString::fromUtf8(relative);
}

// 편집 초안과 저장본의 분리 및 검증 실패 시 보존을 확인하는 함수.
void testDraftAndSave() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary rule directory should exist");
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "new default tree should load");
    if (!initial) return;
    nookbit::JsonRuleSetRepository active(directory.filePath(QStringLiteral("rules.json")), schema);
    expect(active.save(initial.value()).hasValue(), "default tree should initialize user file");

    nookbit::RuleEditorController editor(active, active);
    expect(editor.nodes().size() == 2 && !editor.dirty(), "editor should load two clean roots");
    expect(editor.edit(QStringLiteral("0"), QStringLiteral("images"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("png, jpg"),
                       QStringLiteral("MiB"),
                       QStringLiteral("Pictures/Edited")), "draft edit should succeed");
    expect(editor.dirty(), "draft edit should mark editor dirty");
    const auto beforeSave = active.load();
    expect(beforeSave && beforeSave.value().nodes[0].destination->relativePath ==
               std::filesystem::path(L"Pictures/{yyyy}/{MM}"),
           "draft edit must not change active rules");

    expect(editor.add(QString()), "new root should be added to draft");
    expect(editor.move(QStringLiteral("2"), -1), "new root should move above PDF");
    expect(editor.edit(QStringLiteral("1"), QStringLiteral("text-files"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("txt"),
                       QStringLiteral("MiB"), QStringLiteral("Documents/Text")),
           "new root should be filled before saving");
    expect(editor.save(), "valid draft should save");
    const auto saved = active.load();
    expect(saved && saved.value().nodes.size() == 3 &&
               saved.value().nodes[0].destination->relativePath ==
                   std::filesystem::path(L"Pictures/Edited"),
           "saved rules should contain edits and added root");

    expect(editor.edit(QStringLiteral("0"), QStringLiteral("images"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("png"),
                       QStringLiteral("MiB"), QStringLiteral("../Escape")),
           "unsafe destination should remain only in draft");
    expect(!editor.save(), "unsafe destination must fail validation");
    const auto afterFailure = active.load();
    expect(afterFailure && afterFailure.value().nodes[0].destination->relativePath ==
               std::filesystem::path(L"Pictures/Edited"),
           "failed save must preserve active rules");
    editor.reload();
    expect(!editor.dirty() && editor.detail(QStringLiteral("0")).value(
               QStringLiteral("destination")).toString() == QStringLiteral("Pictures/Edited"),
           "reload should discard unsafe draft");
}

// 내부 가지와 지역·전체 예외를 저장할 수 있는지 확인하는 함수.
void testNestedAndFallback() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    nookbit::JsonRuleSetRepository active(directory.filePath(QStringLiteral("rules.json")), schema);
    const auto initial = defaults.load();
    expect(initial && active.save(initial.value()), "nested fixture should initialize");
    if (!initial) return;
    nookbit::RuleEditorController editor(active, active);
    expect(!editor.add(QStringLiteral("0")), "destination leaf cannot gain children");
    expect(editor.edit(QStringLiteral("0"), QStringLiteral("images"), true,
                       QStringLiteral("size"), QStringLiteral("greaterThan"), QStringLiteral("5"),
                       QStringLiteral("MiB"), QString()),
           "parent destination should be removable");
    expect(editor.add(QStringLiteral("0")), "parent should accept a child");
    expect(editor.edit(QStringLiteral("0/0"), QStringLiteral("png-child"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("png, jpg"),
                       QStringLiteral("MiB"), QStringLiteral("Pictures/PNG")),
           "child should be editable");
    expect(editor.addOtherwise(QStringLiteral("0")), "parent should accept else branch");
    expect(editor.edit(QStringLiteral("0/1"), QStringLiteral("other-images"), true,
                       QString(), QString(), QString(), QStringLiteral("MiB"),
                       QStringLiteral("Pictures/Other")),
           "else branch should accept a destination");
    editor.setGlobalDestination(QStringLiteral("Unsorted"));
    expect(editor.save(), "nested tree and fallback should save");
    const auto restored = active.load();
    expect(restored && restored.value().nodes[0].children.size() == 2 &&
               restored.value().nodes[0].atomicCondition.has_value() &&
               std::holds_alternative<nookbit::SizePredicate>(
                   *restored.value().nodes[0].atomicCondition) &&
               restored.value().nodes[0].children[0].atomicCondition.has_value() &&
               std::holds_alternative<nookbit::ExtensionPredicate>(
                   *restored.value().nodes[0].children[0].atomicCondition) &&
               restored.value().nodes[0].children[1].otherwise &&
               restored.value().globalOtherwise.has_value(),
           "nested children and global fallback should persist");
}
// 화면 그래프를 순서 있는 규칙 초안으로 바꾸고 잘못된 연결은 거부하는지 검사하는 함수.
void testGraphImport() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    nookbit::JsonRuleSetRepository active(directory.filePath(QStringLiteral("rules.json")), schema);
    const auto initial = defaults.load();
    expect(initial && active.save(initial.value()), "graph fixture should initialize");
    if (!initial) return;
    nookbit::RuleEditorController editor(active, active);
    QVariantList blocks{
        QVariantMap{{"id", 1}, {"type", "start"}},
        QVariantMap{{"id", 2}, {"type", "extension"}, {"value", "png, jpg"}},
        QVariantMap{{"id", 3}, {"type", "destination"}, {"value", "Pictures"}},
        QVariantMap{{"id", 4}, {"type", "age"}, {"value", "7"},
                    {"comparison", "atLeast"}},
        QVariantMap{{"id", 5}, {"type", "exclude"}},
    };
    QVariantList edges{
        QVariantMap{{"from", 1}, {"to", 2}},
        QVariantMap{{"from", 2}, {"to", 3}},
        QVariantMap{{"from", 1}, {"to", 4}},
        QVariantMap{{"from", 4}, {"to", 5}},
    };
    expect(editor.importGraph(blocks, edges), "complete graph should become a draft");
    expect(editor.dirty() && editor.nodes().size() == 2 &&
               editor.detail(QStringLiteral("1")).value("exclude").toBool(),
           "graph should preserve sibling order and explicit exclusion");
    const auto beforeSave = active.load();
    expect(beforeSave && beforeSave.value().nodes[0].id != "graph-2",
           "importing a graph must not overwrite active rules");
    expect(editor.save(), "converted graph should save through the existing writer");
    const auto saved = active.load();
    expect(saved && saved.value().nodes.size() == 2 &&
               saved.value().nodes[0].id == "graph-2" &&
               saved.value().nodes[0].destination.has_value() &&
               saved.value().nodes[1].exclude,
           "saved graph should retain ordered destination and excluded leaves");
    edges.removeLast();
    expect(!editor.importGraph(blocks, edges), "orphaned terminal should block import");
    expect(!editor.dirty(), "invalid graph must not change the saved draft");
    edges.append(QVariantMap{{"from", 4}, {"to", 5}});
    edges.append(QVariantMap{{"from", 1}, {"to", 5}});
    expect(!editor.importGraph(blocks, edges), "a node with two parents should block import");
    edges.removeLast();
    blocks.removeAt(2);
    edges.removeAt(1);
    expect(!editor.importGraph(blocks, edges), "a condition without a terminal should block import");
    blocks.insert(2, QVariantMap{{"id", 3}, {"type", "destination"}, {"value", "Pictures"}});
    edges.insert(1, QVariantMap{{"from", 2}, {"to", 3}});
    auto unsafe = blocks.at(2).toMap();
    unsafe.insert(QStringLiteral("value"), QStringLiteral("../Outside"));
    blocks.replace(2, unsafe);
    expect(!editor.importGraph(blocks, edges), "unsafe destination must block import");
}

// 저장 규칙의 노드 왕복과 별도 배치 파일의 위치 복원을 검사하는 함수.
void testGraphRoundTripAndLayout() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "round-trip fixture should load");
    if (!initial) return;
    const auto rulesPath = directory.filePath(QStringLiteral("rules.json"));
    const auto layoutPath = directory.filePath(QStringLiteral("rule-graph-layout.json"));
    nookbit::JsonRuleSetRepository active(rulesPath, schema);
    expect(active.save(initial.value()).hasValue(), "round-trip fixture should save");
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, layoutPath);
    const auto graph = editor.graph();
    expect(graph.value("valid").toBool() && !graph.value("blocks").toList().isEmpty() &&
               !graph.value("edges").toList().isEmpty(),
           "saved rules should expand into connected graph blocks");
    auto blocks = graph.value("blocks").toList();
    const auto edges = graph.value("edges").toList();
    expect(editor.importGraph(blocks, edges), "restored graph should convert without losing rules");
    expect(editor.save(), "restored graph should save through existing rule writer");
    const auto roundTrip = active.load();
    expect(roundTrip && roundTrip.value().nodes.size() == initial.value().nodes.size() &&
               roundTrip.value().nodes[0].id == initial.value().nodes[0].id &&
               roundTrip.value().nodes[0].destination->relativePath ==
                   initial.value().nodes[0].destination->relativePath,
           "graph round trip should preserve original IDs and destinations");
    int movedIndex = -1;
    for (int index = 0; index < blocks.size(); ++index) {
        if (blocks.at(index).toMap().value("ruleId").toString() ==
            QString::fromStdString(initial.value().nodes[0].id)) {
            movedIndex = index;
            break;
        }
    }
    expect(movedIndex >= 0, "root rule should have a matching graph block");
    if (movedIndex < 0) return;
    auto moved = blocks.at(movedIndex).toMap();
    moved.insert(QStringLiteral("x"), 777);
    blocks.replace(movedIndex, moved);
    QFile rulesFile(rulesPath);
    expect(rulesFile.open(QIODevice::ReadOnly), "rule file should be readable before layout save");
    const auto unchangedRules = rulesFile.readAll();
    rulesFile.close();
    expect(editor.saveGraphLayout(blocks, edges), "node positions should save separately");
    expect(rulesFile.open(QIODevice::ReadOnly) && rulesFile.readAll() == unchangedRules,
           "saving node positions must not rewrite classification rules");
    rulesFile.close();
    nookbit::RuleEditorController reopened(active, active, nullptr, nullptr, layoutPath);
    const auto reopenedBlocks = reopened.graph().value("blocks").toList();
    bool positionRestored = false;
    for (const auto& block : reopenedBlocks) {
        const auto item = block.toMap();
        if (item.value("ruleId").toString() ==
                QString::fromStdString(initial.value().nodes[0].id) &&
            item.value("x").toInt() == 777) positionRestored = true;
    }
    expect(positionRestored, "saved node position should survive reopening the editor");
}
} // namespace

// 규칙 편집 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testDraftAndSave();
    testNestedAndFallback();
    testGraphImport();
    testGraphRoundTripAndLayout();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
