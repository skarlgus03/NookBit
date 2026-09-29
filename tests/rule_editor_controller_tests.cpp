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
} // namespace

// 규칙 편집 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testDraftAndSave();
    testNestedAndFallback();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
