#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/folder_flow_graph.hpp"
#include <algorithm>
#include "application/use_cases/test_rule_use_case.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include "application/use_cases/plan_signature.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

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

// 외부 JSON 오류는 위치를 알리고 성공한 후보만 미저장 초안에 반영하는지 검사하는 함수.
void testJsonFileImport() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary import directory should exist");
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "import fixture should load");
    if (!initial) return;
    const QString activePath = directory.filePath(QStringLiteral("active.json"));
    nookbit::JsonRuleSetRepository active(activePath, schema);
    expect(active.save(initial.value()).hasValue(), "active import fixture should initialize");
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, {}, &active);
    const QString candidatePath = directory.filePath(QStringLiteral("candidate.json"));
    const QUrl candidateUrl = QUrl::fromLocalFile(candidatePath);

    QFile candidate(candidatePath);
    expect(candidate.open(QIODevice::WriteOnly), "invalid candidate should open for writing");
    candidate.write("{\n  \"schemaVersion\": 3,\n  \"nodes\": [\n");
    candidate.close();
    const auto syntax = editor.importJsonFile(candidateUrl);
    expect(!syntax.value("ok").toBool() && syntax.value("location").toString().contains("line") &&
               !editor.dirty(), "syntax error should give a line and preserve the draft");

    QJsonObject object{{QStringLiteral("schemaVersion"), 3}};
    expect(candidate.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "schema candidate should open for writing");
    candidate.write(QJsonDocument(object).toJson());
    candidate.close();
    const auto schemaError = editor.importJsonFile(candidateUrl);
    expect(!schemaError.value("ok").toBool() &&
               schemaError.value("location").toString().contains("ruleSetId") && !editor.dirty(),
           "schema error should identify the missing field without changing the draft");

    QFile sample(projectPath("resources/rules/default.v3.json"));
    expect(sample.open(QIODevice::ReadOnly), "sample JSON should open");
    object = QJsonDocument::fromJson(sample.readAll()).object();
    sample.close();
    const QJsonObject validBase = object;
    auto nodes = object.value(QStringLiteral("nodes")).toArray();
    auto duplicate = nodes.at(1).toObject();
    duplicate.insert(QStringLiteral("id"), nodes.at(0).toObject().value(QStringLiteral("id")));
    nodes.replace(1, duplicate);
    object.insert(QStringLiteral("nodes"), nodes);
    expect(candidate.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "duplicate candidate should open for writing");
    candidate.write(QJsonDocument(object).toJson());
    candidate.close();
    const auto duplicateError = editor.importJsonFile(candidateUrl);
    expect(!duplicateError.value("ok").toBool() &&
               duplicateError.value("location").toString() == QStringLiteral("$.nodes[1].id") &&
               !editor.dirty(), "semantic error should identify the duplicate node");

    expect(editor.edit(QStringLiteral("0"), QStringLiteral("images"), true,
                       QStringLiteral("extension"), QString(), QStringLiteral("png, jpg"),
                       QStringLiteral("MiB"), QStringLiteral("Draft/Only")),
           "unsaved draft should be editable before a failed import");
    const auto failedOverDraft = editor.importJsonFile(candidateUrl);
    expect(!failedOverDraft.value("ok").toBool() && editor.dirty() &&
               editor.detail(QStringLiteral("0")).value("destination").toString() ==
                   QStringLiteral("Draft/Only"),
           "failed import must preserve an existing unsaved draft");

    object = validBase;
    nodes = object.value(QStringLiteral("nodes")).toArray();
    auto unfinished = nodes.at(0).toObject();
    unfinished.remove(QStringLiteral("destination"));
    nodes.replace(0, unfinished);
    object.insert(QStringLiteral("nodes"), nodes);
    expect(candidate.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "unfinished candidate should open for writing");
    candidate.write(QJsonDocument(object).toJson());
    candidate.close();
    const auto unfinishedError = editor.importJsonFile(candidateUrl);
    expect(!unfinishedError.value("ok").toBool() &&
               unfinishedError.value("location").toString() == QStringLiteral("규칙 ID: images") &&
               editor.detail(QStringLiteral("0")).value("destination").toString() ==
                   QStringLiteral("Draft/Only"),
           "rule without a terminal should fail import before replacing the draft");

    object = validBase;
    nodes = object.value(QStringLiteral("nodes")).toArray();
    auto image = nodes.at(0).toObject();
    auto destination = image.value(QStringLiteral("destination")).toObject();
    destination.insert(QStringLiteral("path"), QStringLiteral("Imported/Images"));
    image.insert(QStringLiteral("destination"), destination);
    nodes.replace(0, image);
    object.insert(QStringLiteral("nodes"), nodes);
    object.insert(QStringLiteral("globalOtherwise"), QJsonObject{
        {QStringLiteral("path"), QStringLiteral("Unsorted")},
        {QStringLiteral("collisionPolicy"), QStringLiteral("inherit")}});
    expect(candidate.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "valid candidate should open for writing");
    candidate.write(QJsonDocument(object).toJson());
    candidate.close();
    const auto imported = editor.importJsonFile(candidateUrl);
    const auto storedBeforeSave = active.load();
    expect(imported.value("ok").toBool() && editor.dirty() &&
               editor.detail(QStringLiteral("0")).value("destination").toString() ==
                   QStringLiteral("Imported/Images") &&
               storedBeforeSave && storedBeforeSave.value().nodes[0].destination->relativePath ==
                   std::filesystem::path(L"Pictures/{yyyy}/{MM}"),
           "valid import should change only the editable draft");
    const auto importedGraph = editor.graph();
    expect(importedGraph.value("valid").toBool() &&
               editor.importGraph(importedGraph.value("blocks").toList(),
                                  importedGraph.value("edges").toList()),
           "imported JSON should round-trip through the graph editor before saving");
    expect(editor.save(), "imported draft should save only after explicit confirmation");
    const auto storedAfterSave = active.load();
    expect(storedAfterSave && storedAfterSave.value().nodes[0].destination->relativePath ==
               std::filesystem::path(L"Imported/Images") &&
               storedAfterSave.value().globalOtherwise &&
               storedAfterSave.value().globalOtherwise->relativePath ==
                   std::filesystem::path(L"Unsorted"),
           "explicit save should apply the imported rules");
}

// 현재 미저장 그래프를 별도 JSON으로 내보내되 활성 규칙은 보호하는지 검사하는 함수.
void testJsonGraphExport() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary export directory should exist");
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "export test should load default rules");
    if (!initial) return;
    const auto activePath = directory.filePath(QStringLiteral("active.json"));
    nookbit::JsonRuleSetRepository active(activePath, schema);
    expect(active.save(initial.value()).hasValue(), "export test should initialize active rules");
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, {}, &active,
                                        nullptr, &active);
    const auto graph = editor.graph();
    auto blocks = graph.value("blocks").toList();
    const auto edges = graph.value("edges").toList();
    for (auto& item : blocks) {
        auto block = item.toMap();
        if (block.value("type").toString() != QStringLiteral("destination") ||
            block.value("parentRuleId").toString() != QStringLiteral("pdf-documents")) continue;
        block.insert(QStringLiteral("value"), QStringLiteral("Documents/ExportOnly"));
        item = block;
    }
    const auto exportPath = directory.filePath(QStringLiteral("exported.json"));
    const auto exported = editor.exportGraph(blocks, edges, QUrl::fromLocalFile(exportPath));
    const auto exportedRules = active.loadFile(std::filesystem::path(exportPath.toStdWString()));
    const auto activeRules = active.load();
    expect(exported.value("ok").toBool() && exportedRules && activeRules &&
               exportedRules.value().nodes[1].destination->relativePath ==
                   std::filesystem::path(L"Documents/ExportOnly") &&
               activeRules.value().nodes[1].destination->relativePath ==
                   std::filesystem::path(L"Documents/PDF") && !editor.dirty(),
           "export should serialize unsaved graph without applying it");

    const auto activeTarget = editor.exportGraph(blocks, edges, QUrl::fromLocalFile(activePath));
    const auto afterRejectedExport = active.load();
    expect(!activeTarget.value("ok").toBool() &&
               afterRejectedExport &&
               afterRejectedExport.value().nodes[1].destination->relativePath ==
                   std::filesystem::path(L"Documents/PDF"),
           "export must not overwrite the active rule file");
    auto invalidEdges = edges;
    invalidEdges.removeLast();
    const auto invalidPath = directory.filePath(QStringLiteral("invalid.json"));
    const auto invalid = editor.exportGraph(blocks, invalidEdges,
                                            QUrl::fromLocalFile(invalidPath));
    expect(!invalid.value("ok").toBool() && !QFile::exists(invalidPath),
           "invalid graph should not create an export file");
}

// AI 작성 안내서의 예제 규칙이 실제 편집기에 가져와지는지 검사하는 함수.
void testAiAuthoringExample() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary AI example directory should exist");
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "AI example test should load default rules");
    if (!initial) return;
    nookbit::JsonRuleSetRepository active(directory.filePath(QStringLiteral("rules.json")), schema);
    expect(active.save(initial.value()).hasValue(), "AI example test should initialize active rules");

    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::TestRuleUseCase tester(scanner, engine);
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, {}, &active, &tester);
    const auto result = editor.importJsonFile(QUrl::fromLocalFile(
        projectPath("resources/rules/ai-example.v3.json")));
    const auto stored = active.load();
    expect(result.value("ok").toBool() && editor.dirty() &&
               editor.graph().value("valid").toBool() && editor.nodes().size() == 5 &&
               stored && stored.value().id == "default",
           "AI example should import as a valid unsaved draft without changing active rules");
    const auto graph = editor.graph();
    const auto blocks = graph.value("blocks").toList();
    const auto edges = graph.value("edges").toList();
    const auto screenshot = editor.testGraph(blocks, edges, {},
        QStringLiteral("Screenshot.png"), QStringLiteral("1024"), QStringLiteral("0"));
    expect(screenshot.value("destination").toString() ==
               QStringLiteral("Pictures/Screenshots") &&
               screenshot.value("path").toList().size() == 2,
           "nested rule test should show both selected conditions");
    const auto otherPicture = editor.testGraph(blocks, edges, {},
        QStringLiteral("holiday.jpg"), QStringLiteral("1024"), QStringLiteral("0"));
    expect(otherPicture.value("destination").toString() ==
               QStringLiteral("Pictures/Other") &&
               otherPicture.value("path").toList().size() == 2,
           "rule test should follow a local otherwise branch");
    const auto excluded = editor.testGraph(blocks, edges, {},
        QStringLiteral("cache.tmp"), QStringLiteral("0"), QStringLiteral("0"));
    expect(excluded.value("outcome").toString() == QStringLiteral("excluded") &&
               excluded.value("path").toList().size() == 1,
           "rule test should distinguish explicit exclusion from unmatched input");

    QFile guide(projectPath("docs/ai-rule-authoring-v3.md"));
    QFile example(projectPath("resources/rules/ai-example.v3.json"));
    expect(guide.open(QIODevice::ReadOnly) && example.open(QIODevice::ReadOnly),
           "AI guide and example should be readable");
    if (!guide.isOpen() || !example.isOpen()) return;
    const auto text = guide.readAll().replace("\r\n", "\n");
    const auto start = text.indexOf("```json\n");
    const auto end = text.indexOf("\n```", start + 8);
    expect(start >= 0 && end > start, "AI guide should contain one complete JSON example");
    if (start < 0 || end <= start) return;
    const auto embedded = QJsonDocument::fromJson(text.mid(start + 8, end - start - 8));
    const auto fixture = QJsonDocument::fromJson(example.readAll());
    expect(embedded.isObject() && embedded == fixture,
           "AI guide's embedded JSON should match its validated fixture");
}

// 저장 전 그래프를 실제 파일과 가상 메타데이터로 안전하게 시험하는 함수.
void testRuleGraphTesting() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary rule test directory should exist");
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(
        projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    expect(initial.hasValue(), "rule test should load default rules");
    if (!initial) return;
    nookbit::JsonRuleSetRepository active(directory.filePath(QStringLiteral("rules.json")), schema);
    expect(active.save(initial.value()).hasValue(), "rule test should initialize active rules");
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::TestRuleUseCase tester(scanner, engine);
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, {}, &active, &tester);
    const auto graph = editor.graph();
    const auto blocks = graph.value("blocks").toList();
    const auto edges = graph.value("edges").toList();

    const auto synthetic = editor.testGraph(blocks, edges, {}, QStringLiteral("sample.pdf"),
                                            QStringLiteral("10485760"), QStringLiteral("30"));
    expect(synthetic.value("ok").toBool() &&
               synthetic.value("outcome").toString() == QStringLiteral("destination") &&
               synthetic.value("destination").toString() == QStringLiteral("Documents/PDF") &&
               synthetic.value("path").toList().size() == 1,
           "synthetic metadata should follow the unsaved graph and report its path");
    auto unsavedBlocks = blocks;
    for (auto& item : unsavedBlocks) {
        auto block = item.toMap();
        if (block.value("type").toString() != QStringLiteral("destination") ||
            block.value("parentRuleId").toString() != QStringLiteral("pdf-documents")) continue;
        block.insert(QStringLiteral("value"), QStringLiteral("Documents/TestOnly"));
        item = block;
    }
    const auto unsaved = editor.testGraph(unsavedBlocks, edges, {}, QStringLiteral("sample.pdf"),
                                          QStringLiteral("1"), QStringLiteral("0"));
    const auto activeAfterTest = active.load();
    expect(unsaved.value("destination").toString() == QStringLiteral("Documents/TestOnly") &&
               activeAfterTest && activeAfterTest.value().nodes[1].destination->relativePath ==
                   std::filesystem::path(L"Documents/PDF"),
           "testing an unsaved graph should not change the active rules");

    QFile selected(directory.filePath(QStringLiteral("actual.pdf")));
    expect(selected.open(QIODevice::WriteOnly), "actual test file should open");
    selected.write("test metadata");
    selected.close();
    const auto actual = editor.testGraph(blocks, edges, QUrl::fromLocalFile(selected.fileName()),
                                         {}, {}, {});
    expect(actual.value("ok").toBool() &&
               actual.value("destination").toString() == QStringLiteral("Documents/PDF") &&
               selected.exists(), "selected file should be classified without being moved");

    const auto unmatched = editor.testGraph(blocks, edges, {}, QStringLiteral("sample.tmp"),
                                            QStringLiteral("0"), QStringLiteral("0"));
    expect(unmatched.value("ok").toBool() &&
               unmatched.value("outcome").toString() == QStringLiteral("unclassified"),
           "unmatched file should stay unclassified");
    const auto invalid = editor.testGraph(blocks, edges, {}, QStringLiteral("bad/name.pdf"),
                                          QStringLiteral("0"), QStringLiteral("0"));
    expect(!invalid.value("ok").toBool() && !editor.dirty(),
           "invalid metadata should fail without changing the rule draft");
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
    moved.insert(QStringLiteral("x"), -777);
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
            item.value("x").toInt() == -777) positionRestored = true;
    }
    expect(positionRestored, "saved node position should survive reopening the editor");
}
// 새 크기 단위와 기존 단위가 저장·복원 과정에서 바이트 값을 보존하는지 검사하는 함수.
void testSizeUnits() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    nookbit::JsonRuleSetRepository active(directory.filePath("units.json"), schema);
    const auto initial = defaults.load();
    if (!initial || !active.save(initial.value())) { expect(false, "unit fixture initializes"); return; }
    nookbit::RuleEditorController editor(active, active);
    QVariantList edges{QVariantMap{{"from", 1}, {"to", 2}}, QVariantMap{{"from", 2}, {"to", 3}}};
    for (const auto& [unit, factor] : std::initializer_list<std::pair<const char*, qulonglong>>{
            {"KB", 1000}, {"MB", 1000000}, {"GB", 1000000000}, {"MiB", 1048576}, {"B", 1}}) {
        QVariantList blocks{QVariantMap{{"id", 1}, {"type", "start"}},
            QVariantMap{{"id", 2}, {"type", "size"}, {"value", "5"}, {"unit", unit}, {"comparison", "atLeast"}},
            QVariantMap{{"id", 3}, {"type", "destination"}, {"value", "Sorted"}}};
        expect(editor.importGraph(blocks, edges) && editor.save(), "unit graph saves");
        const auto saved = active.load();
        expect(saved && std::get<nookbit::SizePredicate>(*saved.value().nodes[0].atomicCondition).bytes == 5 * factor,
            "unit multiplication preserves exact bytes");
        auto graph = editor.graph();
        bool found = false;
        for (const auto& item : graph.value("blocks").toList()) {
            const auto block = item.toMap();
            if (block.value("type") == "size") found = block.value("unit").toString() == unit && block.value("value") == "5";
        }
        expect(found, "graph restores the exact display unit");
    }
    QVariantList invalid{QVariantMap{{"id", 1}, {"type", "start"}},
        QVariantMap{{"id", 2}, {"type", "size"}, {"value", "5"}, {"unit", "unknown"}, {"comparison", "greaterThan"}},
        QVariantMap{{"id", 3}, {"type", "destination"}, {"value", "Sorted"}}};
    expect(!editor.importGraph(invalid, edges), "unknown units rejected without division by zero");
    auto tooLarge = invalid[1].toMap();
    tooLarge["unit"] = "GB"; tooLarge["value"] = "9007199254740991";
    invalid[1] = tooLarge;
    expect(!editor.importGraph(invalid, edges), "oversized GB conversion rejected before multiplication");
}
// 외부 목적지의 그래프·JSON 왕복과 실행 계획의 사용자 허용 검사를 확인하는 함수.
void testExternalDestinations() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository active(directory.filePath("rules.json"), schema);
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    if (!initial) { expect(false, "default rule fixture loads"); return; }
    expect(active.save(initial.value()).hasValue(), "external test fixture initializes");
    nookbit::RuleEditorController editor(active, active);
    const auto external = directory.filePath("External");
    QVariantList blocks{
        QVariantMap{{"id", 1}, {"type", "start"}},
        QVariantMap{{"id", 2}, {"type", "extension"}, {"value", "txt"}},
        QVariantMap{{"id", 3}, {"type", "destination"}, {"value", external}, {"homeRelative", false}}
    };
    QVariantList edges{QVariantMap{{"from", 1}, {"to", 2}}, QVariantMap{{"from", 2}, {"to", 3}}};
    expect(editor.importGraph(blocks, edges), "absolute destination graph imports");
    expect(editor.save(), "absolute destination JSON saves");
    const auto loaded = active.load();
    expect(loaded && loaded.value().nodes[0].destination->absolute, "absolute base survives JSON load");
    if (!loaded) return;
    const auto exported = editor.graph().value("blocks").toList();
    bool foundAbsolute = false;
    for (const auto& block : exported) {
        const auto value = block.toMap();
        if (value.value("type").toString() == "destination")
            foundAbsolute = !value.value("homeRelative").toBool() && value.value("value").toString() == external;
    }
    expect(foundAbsolute, "graph export preserves absolute checkbox and path");
    nookbit::FileFact file;
    file.absolutePath = std::filesystem::path(directory.filePath("input.txt").toStdWString());
    file.extension = "txt";
    nookbit::ScanSnapshot snapshot;
    snapshot.files.push_back(file);
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, std::filesystem::path(directory.filePath("appdata").toStdWString()));
    nookbit::UserSettings settings;
    settings.homeRoot = std::filesystem::path(directory.filePath("Home").toStdWString());
    const auto denied = planner.buildPlan(snapshot, settings, loaded.value());
    expect(!denied && denied.error().code == "path.destination_outside_home", "JSON never grants external permission");
    const auto oldSignature = nookbit::PlanSignature::settings(settings);
    settings.allowedDestinationRoots = {std::filesystem::path(external.toStdWString())};
    const auto allowed = planner.buildPlan(snapshot, settings, loaded.value());
    expect(allowed && allowed.value().steps[0].destination == settings.allowedDestinationRoots[0] / "input.txt",
           "explicit permission plans the absolute destination without prefixing home");
    expect(oldSignature != nookbit::PlanSignature::settings(settings), "permission changes affect approval signature");
    settings.allowedDestinationRoots.clear();
    expect(!planner.buildPlan(snapshot, settings, loaded.value()), "revoked external permission is rejected");
    auto invalid = blocks[2].toMap();
    invalid["homeRelative"] = true;
    blocks[2] = invalid;
    expect(!editor.importGraph(blocks, edges), "absolute path cannot masquerade as home-relative");
    invalid["homeRelative"] = false; invalid["value"] = "relative/folder";
    blocks[2] = invalid;
    expect(!editor.importGraph(blocks, edges), "absolute mode rejects relative paths");
}
// 파일·폴더 조건의 화면 변환·JSON 왕복·가상/실제 시험과 잘못된 입력 거부를 검사하는 함수.
void testItemTypeRules() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    nookbit::JsonRuleSetRepository active(directory.filePath("rules.json"), schema);
    auto initial = defaults.load();
    if (!initial) { expect(false, "item type fixture"); return; }
    initial.value().globalOtherwise.reset();
    expect(active.save(initial.value()).hasValue(), "item type initial rules");
    nookbit::FilesystemScanner scanner; nookbit::RuleEngine engine; nookbit::TestRuleUseCase tester(scanner, engine);
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, {}, &active, &tester);
    QVariantList blocks{QVariantMap{{"id", 1}, {"type", "start"}},
        QVariantMap{{"id", 2}, {"type", "itemType"}, {"value", "folder"}},
        QVariantMap{{"id", 3}, {"type", "destination"}, {"value", "Folders"}}};
    const QVariantList edges{QVariantMap{{"from", 1}, {"to", 2}}, QVariantMap{{"from", 2}, {"to", 3}}};
    expect(editor.importGraph(blocks, edges) && editor.save(), "folder-only graph saves");
    const auto saved = active.load();
    expect(saved && std::holds_alternative<nookbit::ItemTypePredicate>(*saved.value().nodes[0].atomicCondition) &&
        std::get<nookbit::ItemTypePredicate>(*saved.value().nodes[0].atomicCondition).value == nookbit::ItemType::Folder,
        "JSON round trip preserves folder predicate");
    const auto restored = editor.graph().value("blocks").toList();
    bool restoredType = false;
    for (const auto& item : restored) if (item.toMap().value("type") == "itemType")
        restoredType = item.toMap().value("value") == "folder";
    expect(restoredType, "graph restores item type as a dedicated condition");
    const auto folder = editor.testGraph(blocks, edges, {}, "same-name.png", "0", "0", true);
    const auto file = editor.testGraph(blocks, edges, {}, "same-name.png", "0", "0", false);
    expect(folder.value("destination") == "Folders" && file.value("outcome") == "unclassified", "virtual test distinguishes file and folder");
    const auto actualFolder = directory.filePath("actual-folder");
    expect(QDir().mkpath(actualFolder), "real test folder");
    const auto actual = editor.testGraph(blocks, edges, QUrl::fromLocalFile(actualFolder), {}, {}, {});
    expect(actual.value("destination") == "Folders", "real folder metadata is detected automatically");
    auto invalidBlock = blocks[1].toMap(); invalidBlock["value"] = "both"; blocks[1] = invalidBlock;
    expect(!editor.importGraph(blocks, edges), "invalid item type is rejected in graph");
    QFile serialized(directory.filePath("rules.json"));
    if (serialized.open(QIODevice::ReadOnly)) {
        auto malformed = QJsonDocument::fromJson(serialized.readAll()).object();
        auto nodes = malformed.value("nodes").toArray(); auto node = nodes[0].toObject();
        node["when"] = QJsonObject{{"type", "itemType"}, {"kind", "both"}}; nodes[0] = node; malformed["nodes"] = nodes;
        QFile badFile(directory.filePath("bad.json")); expect(badFile.open(QIODevice::WriteOnly), "invalid JSON fixture");
        badFile.write(QJsonDocument(malformed).toJson()); badFile.close();
        expect(!active.loadFile(std::filesystem::path(badFile.fileName().toStdWString())), "invalid item type rejected on JSON import");
    }
}
// 폴더 흐름의 이름·고유 ID·위치가 저장과 JSON 입출력에서 유지되는지 검사하는 함수.
void testNamedFolderFlowGraph() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    const auto initial = defaults.load();
    if (!initial) { expect(false, "flow initial rules"); return; }
    nookbit::JsonRuleSetRepository active(directory.filePath("rules.json"), schema);
    expect(bool(active.save(initial.value())), "initialize flow rule fixture");
    nookbit::RuleEditorController editor(active, active, nullptr, nullptr, directory.filePath("layout.json"), &active, nullptr, &active);
    auto graph = editor.graph();
    auto blocks = graph.value("blocks").toList();
    auto edges = graph.value("edges").toList();
    int next = graph.value("nextId").toInt();
    nookbit::appendFolderFlowGraph({{"my-contents", "내용물만 정리"}}, {}, blocks, edges, next, 900);
    for (auto& value : blocks) {
        auto block = value.toMap();
        if (block.value("type") == "folderScan") {
            block["maxDepth"] = 1;
            value = block;
        }
    }
    expect(editor.importGraph(blocks, edges) && editor.save(), "save named folder flow from graph");
    expect(editor.saveGraphLayout(blocks, edges), "save positions of flow nodes");
    auto saved = active.load();
    expect(saved && saved.value().folderFlows.size() == 1 && saved.value().folderFlows[0].id == "my-contents", "active JSON retains flow identity");
    editor.reload();
    const auto restored = editor.graph().value("blocks").toList();
    bool found = false;
    bool depthRestored = false;
    for (const auto& value : restored) {
        const auto block = value.toMap();
        if (block.value("type") == "flowStart") found = block.value("value") == "내용물만 정리" && block.value("y").toDouble() == 900;
        if (block.value("type") == "folderScan") depthRestored = block.value("maxDepth") == 1;
    }
    expect(found, "reload restores group name and layout");
    expect(depthRestored, "reload restores the traversal depth on the canvas");
    const auto exported = editor.exportGraph(blocks, edges, QUrl::fromLocalFile(directory.filePath("export.json")));
    expect(exported.value("ok").toBool(), "export includes folder flows");
    const auto imported = editor.importJsonFile(QUrl::fromLocalFile(directory.filePath("export.json")));
    expect(imported.value("ok").toBool(), "exported folder flow imports through normal validation");
    auto badEdges = edges;
    badEdges.append(QVariantMap{{"from", 1}, {"to", next - 3}});
    expect(!editor.importGraph(blocks, badEdges), "editor rejects cross-flow wiring before replacing draft");
    // 기본 프리셋의 호출을 제거하고 실제 조건과 목적지로 바꿔 저장한다.
    auto custom = editor.graph();
    auto customBlocks = custom.value("blocks").toList();
    auto customEdges = custom.value("edges").toList();
    int scanId = -1, callId = -1, maximumId = 0;
    QString scanKey;
    for (const auto& value : customBlocks) {
        const auto block = value.toMap(); const int id = block.value("id").toInt(); maximumId = std::max(maximumId, id);
        if (block.value("flowId") != "my-contents") continue;
        if (block.value("type") == "folderScan") { scanId = id; scanKey = block.value("flowNodeId").toString(); }
        if (block.value("type") == "callFlow") callId = id;
    }
    for (int i = customBlocks.size() - 1; i >= 0; --i) if (customBlocks[i].toMap().value("id").toInt() == callId) customBlocks.removeAt(i);
    for (int i = customEdges.size() - 1; i >= 0; --i) if (customEdges[i].toMap().value("to").toInt() == callId) customEdges.removeAt(i);
    customBlocks.append(QVariantMap{{"id", maximumId + 1}, {"flowId", "my-contents"}, {"flowNodeId", "png"}, {"type", "extension"}, {"value", "png"}, {"x", 900}, {"y", 900}});
    customBlocks.append(QVariantMap{{"id", maximumId + 2}, {"flowId", "my-contents"}, {"flowNodeId", "target"}, {"type", "destination"}, {"value", "MyImages"}, {"x", 1200}, {"y", 900}});
    customEdges.append(QVariantMap{{"from", scanId}, {"to", maximumId + 1}});
    customEdges.append(QVariantMap{{"from", maximumId + 1}, {"to", maximumId + 2}});
    expect(editor.importGraph(customBlocks, customEdges) && editor.save(), "custom flow connections save instead of reverting to fixed pipeline");
    const auto customRules = active.load();
    nookbit::FileFact file; file.absolutePath = L"C:/test/photo.png"; file.processingFlowId = "my-contents"; file.processingAfterNode = scanKey.toStdString();
    nookbit::RuleEngine flowEngine;
    if (customRules) {
        const auto decision = flowEngine.decide(file, customRules.value(), nookbit::MultiMatchPolicy::FirstMatch, 0);
        expect(decision && decision.value() && decision.value()->destinationRelativePath == L"MyImages", "saved editor graph changes actual classification");
    }
}
// 경로 조건의 편집·JSON 왕복·잘못된 입력 및 처리 흐름 재사용을 검사하는 함수.
void testSourceFolderGraph() {
    QTemporaryDir directory;
    const auto schema=projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"),schema);
    nookbit::JsonRuleSetRepository active(directory.filePath("rules.json"),schema);
    auto initial=defaults.load(); if(!initial){expect(false,"source fixture");return;}
    initial.value().globalOtherwise.reset(); expect(active.save(initial.value()).hasValue(),"source fixture saved");
    nookbit::RuleEditorController editor(active,active);
    QVariantList blocks{QVariantMap{{"id",1},{"type","start"}},
        QVariantMap{{"id",2},{"type","sourceFolder"},{"value","C:/Input/프로젝트"},{"recursive",false}},
        QVariantMap{{"id",3},{"type","destination"},{"value","Projects"}}};
    const QVariantList edges{QVariantMap{{"from",1},{"to",2}},QVariantMap{{"from",2},{"to",3}}};
    expect(editor.importGraph(blocks,edges)&&editor.save(),"source graph saves through JSON validation");
    const auto loaded=active.load();
    expect(loaded && std::holds_alternative<nookbit::SourceFolderPredicate>(*loaded.value().nodes[0].atomicCondition)
        && !std::get<nookbit::SourceFolderPredicate>(*loaded.value().nodes[0].atomicCondition).recursive,"source depth survives JSON round trip");
    bool restored=false;
    for(const auto& b:editor.graph().value("blocks").toList()) if(b.toMap().value("type")=="sourceFolder")
        restored=b.toMap().value("value")=="C:/Input/프로젝트" && !b.toMap().value("recursive").toBool();
    expect(restored,"source graph preserves Unicode path and depth");
    QFile serialized(directory.filePath("rules.json"));
    if (serialized.open(QIODevice::ReadOnly)) {
        const auto original=QJsonDocument::fromJson(serialized.readAll()).object();
        serialized.close();
        for (const auto& invalid: {QJsonObject{{"type","sourceFolder"},{"path","relative/path"},{"recursive",true}},
            QJsonObject{{"type","sourceFolder"},{"path","C:/Input"},{"recursive","false"}},
            QJsonObject{{"type","sourceFolder"},{"path","C:/Input"}},
            QJsonObject{{"type","sourceFolder"},{"path","C:/Input/../Other"},{"recursive",true}}}) {
            auto document=original; auto nodes=document["nodes"].toArray();auto node=nodes[0].toObject();
            node["when"]=invalid;nodes[0]=node;document["nodes"]=nodes;
            QFile bad(directory.filePath("bad.json"));expect(bad.open(QIODevice::WriteOnly),"source invalid fixture");
            bad.write(QJsonDocument(document).toJson());bad.close();
            expect(!active.loadFile(std::filesystem::path(bad.fileName().toStdWString())),"invalid source JSON rejected");
        }
    }
    auto flowBlocks=blocks;
    flowBlocks.append(QVariantMap{{"id",4},{"type","flowStart"},{"flowId","source-flow"},{"flowNodeId","input"},{"value","Source flow"}});
    flowBlocks.append(QVariantMap{{"id",5},{"type","sourceFolder"},{"flowId","source-flow"},{"flowNodeId","source"},{"value","C:/Input"},{"recursive",false}});
    flowBlocks.append(QVariantMap{{"id",6},{"type","destination"},{"flowId","source-flow"},{"flowNodeId","dest"},{"value","Other"}});
    auto flowEdges=edges;flowEdges.append(QVariantMap{{"from",4},{"to",5}});flowEdges.append(QVariantMap{{"from",5},{"to",6}});
    const bool flowSaved=editor.importGraph(flowBlocks,flowEdges)&&editor.save();
    if (!flowSaved) std::cerr << editor.message().toStdString() << "\n";
    expect(flowSaved,"source predicate works inside named processing flows");
    bool flowRestored=false;
    for(const auto& block:editor.graph().value("blocks").toList()) {
        const auto map=block.toMap();
        if(map.value("type")=="sourceFolder" && map.value("flowId")=="source-flow")
            flowRestored=map.value("value")=="C:/Input" && !map.value("recursive").toBool();
    }
    expect(flowRestored,"named processing flow preserves source-folder depth");

    for(const auto& invalid: {QString(),QString("relative/folder"),QString("C:/Input/../Other")}) {
        auto b=blocks[1].toMap(); b["value"]=invalid; blocks[1]=b;
        expect(!editor.importGraph(blocks,edges),"source graph rejects empty relative and parent traversal paths");
    }
    auto b=blocks[1].toMap();b["value"]="C:/Input";b["recursive"]="false";blocks[1]=b;
    expect(!editor.importGraph(blocks,edges),"source graph rejects string booleans");
}

// 조건 계산 선의 중첩·저장·재복원·배치·잘못된 연결을 검사하는 함수.
void testLogicalGraph() {
    QTemporaryDir directory;const auto schema=projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"),schema);
    nookbit::JsonRuleSetRepository active(directory.filePath("rules.json"),schema);
    auto initial=defaults.load();if(!initial){expect(false,"logical fixture");return;}
    initial.value().globalOtherwise.reset();expect(active.save(initial.value()).hasValue(),"logical fixture saved");
    nookbit::RuleEditorController editor(active,active,nullptr,nullptr,directory.filePath("layout.json"));
    QVariantList blocks{QVariantMap{{"id",1},{"type","start"},{"x",0},{"y",0}},
        QVariantMap{{"id",2},{"type","all"},{"x",600},{"y",0}},
        QVariantMap{{"id",3},{"type","any"},{"x",300},{"y",0}},
        QVariantMap{{"id",4},{"type","extension"},{"value","png"},{"x",0},{"y",100}},
        QVariantMap{{"id",5},{"type","name"},{"value","report"},{"x",0},{"y",200}},
        QVariantMap{{"id",6},{"type","size"},{"value","10"},{"unit","B"},{"comparison","atLeast"},{"x",300},{"y",300}},
        QVariantMap{{"id",7},{"type","destination"},{"value","Matched"},{"x",900},{"y",0}}};
    QVariantList edges{QVariantMap{{"from",1},{"to",2}},QVariantMap{{"from",2},{"to",7}},
        QVariantMap{{"from",4},{"to",3},{"kind","predicate"}},QVariantMap{{"from",5},{"to",3},{"kind","predicate"}},
        QVariantMap{{"from",3},{"to",2},{"kind","predicate"}},QVariantMap{{"from",6},{"to",2},{"kind","predicate"}}};
    expect(editor.importGraph(blocks,edges)&&editor.save(),"nested condition graph saves");
    expect(editor.saveGraphLayout(blocks,edges),"nested condition positions save");
    const auto loaded=active.load();expect(loaded && std::holds_alternative<nookbit::LogicalPredicatePtr>(*loaded.value().nodes[0].atomicCondition),"JSON logical condition round trip");
    QFile encoded(directory.filePath("rules.json"));
    if (encoded.open(QIODevice::ReadOnly)) {
        const auto original=QJsonDocument::fromJson(encoded.readAll()).object();encoded.close();
        auto shallow=QJsonObject{{"type","all"},{"conditions",QJsonArray{QJsonObject{{"type","nameContains"},{"text","x"}}}}};
        QJsonObject deep{{"type","nameContains"},{"text","x"}};
        for(int i=0;i<34;++i)deep=QJsonObject{{"type","any"},{"conditions",QJsonArray{deep,QJsonObject{{"type","nameContains"},{"text","x"}}}}};
        for(const auto& invalid:{shallow,deep}) {
            auto document=original;auto nodes=document["nodes"].toArray();auto node=nodes[0].toObject();node["when"]=invalid;nodes[0]=node;document["nodes"]=nodes;
            QFile bad(directory.filePath("invalid-logical.json"));expect(bad.open(QIODevice::WriteOnly),"invalid logical fixture");bad.write(QJsonDocument(document).toJson());bad.close();
            expect(!active.loadFile(std::filesystem::path(bad.fileName().toStdWString())),"invalid logical arity or nesting rejected on JSON import");
        }
    }
    const auto graph=editor.graph();int predicateEdges=0;bool positionRestored=false;
    for(const auto& edge:graph.value("edges").toList())if(edge.toMap().value("kind")=="predicate")++predicateEdges;
    for(const auto& value:graph.value("blocks").toList()){const auto b=value.toMap();if(b.value("type")=="name")positionRestored=b.value("x").toInt()==0&&b.value("y").toInt()==200;}
    expect(predicateEdges==4&&positionRestored,"logical graph restores separate conditions and exact operand positions");
    expect(editor.importGraph(graph.value("blocks").toList(),graph.value("edges").toList())&&editor.save(),"restored logical graph is editable and saves again");
    auto invalid=edges;invalid.removeLast();expect(!editor.importGraph(blocks,invalid),"one-input AND rejected");
    invalid=edges;invalid.append(QVariantMap{{"from",1},{"to",4}});expect(!editor.importGraph(blocks,invalid),"condition cannot also participate in execution flow");
    invalid=edges;invalid.append(QVariantMap{{"from",7},{"to",2},{"kind","predicate"}});expect(!editor.importGraph(blocks,invalid),"destination cannot become an operand");
    invalid=edges;invalid.append(QVariantMap{{"from",2},{"to",3},{"kind","predicate"}});expect(!editor.importGraph(blocks,invalid),"logical cycles rejected");
    auto flowBlocks=blocks;for(int i=1;i<flowBlocks.size();++i){auto b=flowBlocks[i].toMap();b["flowId"]="logical-flow";b["flowNodeId"]=QString("n%1").arg(i);flowBlocks[i]=b;}
    flowBlocks.append(QVariantMap{{"id",8},{"type","flowStart"},{"flowId","logical-flow"},{"flowNodeId","input"},{"flowInput","item"},{"value","Logic flow"},{"x",0},{"y",0}});
    // 기본 트리도 유지하고 같은 논리 묶음을 별도 흐름에서 재사용한다.
    flowBlocks.append(QVariantMap{{"id",9},{"type","name"},{"value","base"},{"x",0},{"y",0}});
    flowBlocks.append(QVariantMap{{"id",10},{"type","exclude"},{"x",0},{"y",0}});
    auto flowEdges=edges;flowEdges[0]=QVariantMap{{"from",8},{"to",2}};flowEdges.append(QVariantMap{{"from",1},{"to",9}});flowEdges.append(QVariantMap{{"from",9},{"to",10}});
    expect(editor.importGraph(flowBlocks,flowEdges)&&editor.save(),"logical predicates work in named processing graphs");
}
// 모순과 가려짐을 경고하고 구조 오류는 저장을 막는지 검사하는 함수.
void testRuleInspection() {
    QTemporaryDir directory;
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    nookbit::JsonRuleSetRepository defaults(projectPath("resources/rules/default.v3.json"), schema);
    nookbit::JsonRuleSetRepository active(directory.filePath("rules.json"), schema);
    const auto initial = defaults.load();
    if (!initial || !active.save(initial.value())) {
        expect(false, "inspection fixture saved");
        return;
    }
    nookbit::RuleEditorController editor(active, active);
    QVariantList blocks{
        QVariantMap{{"id", 1}, {"type", "start"}},
        QVariantMap{{"id", 2}, {"type", "extension"}, {"value", "PNG"}},
        QVariantMap{{"id", 3}, {"type", "exclude"}},
        QVariantMap{{"id", 4}, {"type", "all"}},
        QVariantMap{{"id", 5}, {"type", "extension"}, {"value", "png"}},
        QVariantMap{{"id", 6}, {"type", "size"}, {"value", "5"}, {"unit", "MB"}, {"comparison", "greaterThan"}},
        QVariantMap{{"id", 7}, {"type", "destination"}, {"value", "Images"}}
    };
    const QVariantList edges{
        QVariantMap{{"from", 1}, {"to", 2}}, QVariantMap{{"from", 2}, {"to", 3}},
        QVariantMap{{"from", 1}, {"to", 4}}, QVariantMap{{"from", 4}, {"to", 7}},
        QVariantMap{{"from", 5}, {"to", 4}, {"kind", "predicate"}},
        QVariantMap{{"from", 6}, {"to", 4}, {"kind", "predicate"}}
    };
    auto report = editor.inspectGraph(blocks, edges);
    auto warnings = report.value("warnings").toList();
    expect(warnings.size() == 1 && warnings.first().toMap().value("blockId") == 4
        && warnings.first().toMap().value("earlierBlockId") == 2, "earlier PNG shadows narrower AND branch");
    expect(editor.importGraph(blocks, edges) && editor.save(), "warnings allow save");

    blocks[1] = QVariantMap{{"id", 2}, {"type", "name"}, {"value", "report"}};
    expect(editor.inspectGraph(blocks, edges).value("warnings").toList().isEmpty(),
        "unknown earlier condition does not falsely shadow a branch");
    blocks[4] = QVariantMap{{"id", 5}, {"type", "size"}, {"value", "5"}, {"unit", "MB"}, {"comparison", "atMost"}};
    warnings = editor.inspectGraph(blocks, edges).value("warnings").toList();
    expect(warnings.size() == 1 && warnings.first().toMap().value("blockId") == 4
        && warnings.first().toMap().value("earlierBlockId") == 0, "exclusive size boundaries are contradictory");
    auto inclusive = blocks[5].toMap();
    inclusive["comparison"] = "atLeast";
    blocks[5] = inclusive;
    expect(editor.inspectGraph(blocks, edges).value("warnings").toList().isEmpty(),
        "equal inclusive boundaries have a valid match");
    auto broken = edges;
    broken.removeLast();
    report = editor.inspectGraph(blocks, broken);
    expect(!report.value("error").toString().isEmpty() && report.value("blockId") == 4
        && !editor.importGraph(blocks, broken), "incomplete AND reports a navigable structural error and cannot save");
}
} // namespace

// 규칙 편집 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testDraftAndSave();
    testJsonFileImport();
    testJsonGraphExport();
    testAiAuthoringExample();
    testRuleGraphTesting();
    testNestedAndFallback();
    testGraphImport();
    testGraphRoundTripAndLayout();
    testSizeUnits();
    testExternalDestinations();
    testLogicalGraph();
    testRuleInspection();
    testSourceFolderGraph();
    testItemTypeRules();
    testNamedFolderFlowGraph();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
