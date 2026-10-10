#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "application/use_cases/test_rule_use_case.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCoreApplication>
#include <QFile>
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
    const auto text = guide.readAll();
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
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
