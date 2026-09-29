#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_settings_repository.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "modules/rules/rule_engine.hpp"
#include "application/use_cases/plan_signature.hpp"

#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <filesystem>
#include <chrono>
#include <iostream>
#include <string>

namespace {

int failures = 0;

// 조건이 맞지 않을 때 테스트 실패를 출력하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트 내부의 예제 또는 Schema 절대 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

// JSON 객체를 임시 테스트 파일에 쓰는 함수.
bool writeObject(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(QJsonDocument(object).toJson()) > 0;
}

// 예제 파일의 JSON 객체를 읽어 변형 테스트에 사용하는 함수.
QJsonObject readExample(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// 설정과 규칙 예제가 코어 모델로 정확히 읽히는지 검사하는 함수.
void testExampleFilesLoad() {
    nookbit::JsonSettingsRepository settings(
        projectPath("resources/settings.example.json"),
        projectPath("schemas/settings.schema.json"));
    nookbit::JsonRuleSetRepository rules(
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"));

    auto loadedSettings = settings.load();
    auto loadedRules = rules.load();
    expect(loadedSettings.hasValue(), "example settings should load");
    expect(loadedRules.hasValue(), "example rules should load");
    if (loadedSettings) {
        expect(loadedSettings.value().watchRoots.size() == 1,
               "example settings should contain one enabled watch root");
        expect(loadedSettings.value().defaultCollisionPolicy == nookbit::CollisionPolicy::Rename,
               "example settings should select rename policy");
    }
    if (loadedRules) {
        expect(loadedRules.value().id == "default", "rule-set ID should load");
        expect(loadedRules.value().rules.size() == 2, "both example rules should load");
        if (loadedRules.value().rules.size() == 2) {
            expect(loadedRules.value().rules[0].extensions.size() == 4,
                   "image extensions should load");
            expect(loadedRules.value().rules[1].destinationRelativePath ==
                       std::filesystem::path(L"Documents/PDF"),
                   "PDF destination should load");
        }
    }
    if (loadedSettings && loadedRules) {
        using namespace std::chrono;
        nookbit::FileFact file;
        file.absolutePath = L"C:/Users/example/Downloads/photo.jpg";
        file.relativePath = L"photo.jpg";
        file.extension = "jpg";
        file.fingerprint.modifiedUtcMs = duration_cast<milliseconds>(
            sys_days{year{2026} / 9 / 23}.time_since_epoch()).count();
        nookbit::RuleEngine engine;
        const auto decision = engine.decide(
            file, loadedRules.value(), loadedSettings.value().multiMatchPolicy,
            file.fingerprint.modifiedUtcMs);
        expect(decision.hasValue() && decision.value().has_value(),
               "loaded JSON rules should classify an image");
        if (decision && decision.value()) {
            expect(decision.value()->matchedRuleId == "images",
                   "JSON image rule should be selected");
            expect(decision.value()->destinationRelativePath == L"Pictures/2026/09",
                   "JSON destination date tokens should be expanded");
        }
    }
    expect(!settings.save({}).hasValue(),
           "incomplete settings model must not overwrite the JSON file");
}

// 필수 안전 상수와 예기치 않은 필드를 Schema가 거부하는지 검사하는 함수.
void testInvalidSettingsAreRejected() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("settings.json"));
    auto object = readExample(projectPath("resources/settings.example.json"));
    auto safety = object.value(QStringLiteral("safety")).toObject();
    safety.insert(QStringLiteral("permanentDeleteAllowed"), true);
    object.insert(QStringLiteral("safety"), safety);
    expect(writeObject(path, object), "invalid settings fixture should be written");
    nookbit::JsonSettingsRepository repository(path, projectPath("schemas/settings.schema.json"));
    auto result = repository.load();
    expect(!result.hasValue() && result.error().code == "json.schema_invalid",
           "unsafe setting should be rejected");

    object = readExample(projectPath("resources/settings.example.json"));
    object.insert(QStringLiteral("unexpected"), 1);
    expect(writeObject(path, object), "unknown-field fixture should be written");
    result = repository.load();
    expect(!result.hasValue() && result.error().code == "json.schema_invalid",
           "unknown settings field should be rejected");

    object = readExample(projectPath("resources/settings.example.json"));
    auto paths = object.value(QStringLiteral("paths")).toObject();
    auto folders = paths.value(QStringLiteral("watchFolders")).toArray();
    auto secondFolder = folders.at(0).toObject();
    secondFolder.insert(QStringLiteral("path"), QStringLiteral("C:/Users/example/Desktop"));
    secondFolder.insert(QStringLiteral("recursive"), true);
    folders.append(secondFolder);
    paths.insert(QStringLiteral("watchFolders"), folders);
    object.insert(QStringLiteral("paths"), paths);
    expect(writeObject(path, object), "mixed-recursion fixture should be written");
    result = repository.load();
    expect(!result.hasValue() && result.error().code == "settings.mixed_recursive_unsupported",
           "mixed watch-folder recursion should be rejected until supported");

    object = readExample(projectPath("resources/settings.example.json"));
    auto organization = object.value(QStringLiteral("organization")).toObject();
    organization.insert(QStringLiteral("multiMatchPolicy"), QStringLiteral("abort"));
    organization.insert(QStringLiteral("unmatchedPolicy"), QStringLiteral("abort"));
    object.insert(QStringLiteral("organization"), organization);
    expect(writeObject(path, object), "abort-policy fixture should be written");
    result = repository.load();
    expect(result.hasValue() &&
               result.value().multiMatchPolicy == nookbit::MultiMatchPolicy::Abort,
           "settings should load the multiple-match policy");
    expect(result.hasValue() &&
               result.value().unmatchedPolicy == nookbit::UnmatchedPolicy::Abort,
           "settings should load the unmatched-file policy");
}

// 잘못된 목적지와 중복 규칙 ID를 거부하는지 검사하는 함수.
void testInvalidRulesAreRejected() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("rules.json"));
    auto object = readExample(projectPath("resources/rules/default.example.json"));
    auto entries = object.value(QStringLiteral("rules")).toArray();
    auto rule = entries.at(0).toObject();
    auto then = rule.value(QStringLiteral("then")).toObject();
    then.insert(QStringLiteral("destination"), QStringLiteral("../escaped"));
    rule.insert(QStringLiteral("then"), then);
    entries.replace(0, rule);
    object.insert(QStringLiteral("rules"), entries);
    expect(writeObject(path, object), "unsafe-rules fixture should be written");
    nookbit::JsonRuleSetRepository repository(path, projectPath("schemas/rules.schema.json"));
    auto result = repository.load();
    expect(!result.hasValue() && result.error().code == "json.schema_invalid",
           "parent-path destination should be rejected");

    object = readExample(projectPath("resources/rules/default.example.json"));
    entries = object.value(QStringLiteral("rules")).toArray();
    rule = entries.at(1).toObject();
    rule.insert(QStringLiteral("id"), entries.at(0).toObject().value(QStringLiteral("id")));
    entries.replace(1, rule);
    object.insert(QStringLiteral("rules"), entries);
    expect(writeObject(path, object), "duplicate-rules fixture should be written");
    result = repository.load();
    expect(!result.hasValue() && result.error().code == "rules.duplicate_id",
           "duplicate rule IDs should be rejected");

    object = readExample(projectPath("resources/rules/default.example.json"));
    entries = object.value(QStringLiteral("rules")).toArray();
    rule = entries.at(0).toObject();
    auto when = rule.value(QStringLiteral("when")).toObject();
    when.insert(QStringLiteral("minimumBytes"), 20);
    when.insert(QStringLiteral("maximumBytes"), 10);
    rule.insert(QStringLiteral("when"), when);
    entries.replace(0, rule);
    object.insert(QStringLiteral("rules"), entries);
    expect(writeObject(path, object), "reversed-range fixture should be written");
    result = repository.load();
    expect(!result.hasValue() && result.error().code == "rules.range_invalid",
           "reversed rule range should be rejected");
}

// 트리 JSON의 순서·중첩·else를 읽고 잘못된 구조를 거부하는지 검사하는 함수.
void testRuleTreeLoadsAndRejectsInvalidNodes() {
    const auto schema = projectPath("schemas/rules-tree.schema.json");
    const auto example = projectPath("resources/rules/tree.example.json");
    nookbit::JsonRuleSetRepository repository(example, schema);
    const auto loaded = repository.load();
    expect(loaded.hasValue() && loaded.value().schemaVersion == 2 &&
               loaded.value().nodes.size() == 4,
           "tree example should load in its saved order");
    if (loaded) {
        nookbit::FileFact file;
        file.absolutePath = L"C:/Watch/photo.png";
        file.relativePath = L"photo.png";
        file.extension = "png";
        file.fingerprint.size = 6 * 1024 * 1024;
        file.fingerprint.modifiedUtcMs = 1780099200000;
        nookbit::RuleEngine engine;
        const auto decision = engine.decide(file, loaded.value(),
            nookbit::MultiMatchPolicy::Abort, file.fingerprint.modifiedUtcMs);
        expect(decision && decision.value() &&
                   decision.value()->matchedRuleId == "large-png" &&
                   decision.value()->destinationRelativePath == L"Pictures/Large PNG",
               "tree JSON should classify through a matching child");
    }
    QTemporaryDir directory;
    expect(directory.isValid(), "tree-rules directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("tree.json"));
    auto object = readExample(example);
    auto nodes = object.value(QStringLiteral("nodes")).toArray();
    auto second = nodes.at(1).toObject();
    second.insert(QStringLiteral("destination"), QJsonObject{
        {QStringLiteral("path"), QStringLiteral("Images")},
        {QStringLiteral("collisionPolicy"), QStringLiteral("inherit")}});
    nodes.replace(1, second);
    object.insert(QStringLiteral("nodes"), nodes);
    expect(writeObject(path, object), "invalid internal destination should be written");
    nookbit::JsonRuleSetRepository invalid(path, schema);
    const auto rejected = invalid.load();
    expect(!rejected && rejected.error().code == "rules.internal_destination",
           "an internal node must not also have a destination");
    object = readExample(example);
    nodes = object.value(QStringLiteral("nodes")).toArray();
    second = nodes.at(1).toObject();
    auto children = second.value(QStringLiteral("children")).toArray();
    auto duplicateElse = children.at(1).toObject();
    duplicateElse.insert(QStringLiteral("id"), QStringLiteral("another-else"));
    children.append(duplicateElse);
    second.insert(QStringLiteral("children"), children);
    nodes.replace(1, second);
    object.insert(QStringLiteral("nodes"), nodes);
    expect(writeObject(path, object), "duplicate else should be written");
    const auto duplicate = invalid.load();
    expect(!duplicate && duplicate.error().code == "rules.invalid_otherwise",
           "only one else is allowed among siblings");
    object = readExample(example);
    nodes = object.value(QStringLiteral("nodes")).toArray();
    auto first = nodes.at(0).toObject();
    auto destination = first.value(QStringLiteral("destination")).toObject();
    destination.insert(QStringLiteral("path"), QStringLiteral("../Escape"));
    first.insert(QStringLiteral("destination"), destination);
    nodes.replace(0, first);
    object.insert(QStringLiteral("nodes"), nodes);
    expect(writeObject(path, object), "unsafe tree destination should be written");
    const auto unsafe = invalid.load();
    expect(!unsafe && unsafe.error().code == "json.schema_invalid",
           "tree schema should reject a parent-path destination");
}

// 편집한 트리를 안전하게 저장·복원하고 잘못된 편집은 기존 파일을 보존하는지 검사하는 함수.
void testRuleTreeSaveRoundTrip() {
    nookbit::JsonRuleSetRepository example(
        projectPath("resources/rules/tree.example.json"),
        projectPath("schemas/rules-tree.schema.json"));
    auto loaded = example.load();
    expect(loaded.hasValue(), "tree fixture should load before a round trip");
    if (!loaded) return;
    QTemporaryDir directory;
    expect(directory.isValid(), "tree-save directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("custom-rules.json"));
    nookbit::JsonRuleSetRepository repository(path, projectPath("schemas/rules-tree.schema.json"));
    auto edited = loaded.value();
    edited.nodes[0].destination->relativePath = L"Custom/Large";
    edited.globalOtherwise = nookbit::RuleDestination{L"Unsorted", std::nullopt};
    expect(repository.save(edited).hasValue(), "edited tree should save atomically");
    auto restored = repository.load();
    expect(restored && nookbit::PlanSignature::rules(restored.value()) ==
                           nookbit::PlanSignature::rules(edited),
           "saved tree order, conditions, leaves, and global else should survive reload");
    QFile savedFile(path);
    expect(savedFile.open(QIODevice::ReadOnly), "saved tree should be readable");
    const auto originalBytes = savedFile.readAll();
    savedFile.close();
    edited.nodes[0].destination->relativePath = L"../Escape";
    const auto invalid = repository.save(edited);
    expect(!invalid && invalid.error().code == "rules.destination_invalid",
           "unsafe edited destination must be refused before writing");
    expect(savedFile.open(QIODevice::ReadOnly) && savedFile.readAll() == originalBytes,
           "rejected save must preserve the previous complete JSON file");
    savedFile.close();
    nookbit::JsonRuleSetRepository bundled(
        QStringLiteral(":/nookbit/rules.json"), projectPath("schemas/rules-tree.schema.json"));
    expect(!bundled.save(loaded.value()).hasValue(),
           "bundled resource must not be treated as a writable rule file");
}

// v3 JSON이 조건 종류별 필드를 하나씩 받고 잘못 섞인 필드를 거부하는지 검사하는 함수.
void testAtomicTreeFormat() {
    const auto schema = projectPath("schemas/rules-tree-v3.schema.json");
    const auto defaultPath = projectPath("resources/rules/default.v3.json");
    nookbit::JsonRuleSetRepository defaults(defaultPath, schema);
    const auto loaded = defaults.load();
    expect(loaded && loaded.value().schemaVersion == 3 &&
               loaded.value().nodes.size() == 2 &&
               loaded.value().nodes[0].atomicCondition.has_value(),
           "v3 default should load one condition per node");
    nookbit::JsonRuleSetRepository oldDefaults(
        projectPath("resources/rules/default.tree.json"),
        projectPath("schemas/rules-tree.schema.json"));
    const auto old = oldDefaults.load();
    expect(old.hasValue(), "old default should load for behavior comparison");
    if (loaded && old) {
        nookbit::RuleEngine engine;
        for (const auto& extension : {"jpg", "jpeg", "png", "webp", "pdf", "txt"}) {
            nookbit::FileFact file;
            file.absolutePath = std::filesystem::path(L"C:/Input/sample.jpg");
            file.relativePath = std::filesystem::path(L"sample.jpg");
            file.extension = extension;
            file.fingerprint.modifiedUtcMs = 1780099200000;
            const auto before = engine.decide(file, old.value(),
                nookbit::MultiMatchPolicy::FirstMatch, file.fingerprint.modifiedUtcMs);
            const auto after = engine.decide(file, loaded.value(),
                nookbit::MultiMatchPolicy::FirstMatch, file.fingerprint.modifiedUtcMs);
            expect(before && after && before.value().has_value() == after.value().has_value() &&
                       (!before.value() || before.value()->destinationRelativePath ==
                                           after.value()->destinationRelativePath),
                   std::string("default v3 should preserve destination for ") + extension);
        }
    }
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("atomic.json"));
    auto object = readExample(defaultPath);
    auto nodes = object.value(QStringLiteral("nodes")).toArray();
    auto first = nodes.at(0).toObject();
    auto when = first.value(QStringLiteral("when")).toObject();
    when.insert(QStringLiteral("bytes"), 5242880);
    first.insert(QStringLiteral("when"), when);
    nodes.replace(0, first);
    object.insert(QStringLiteral("nodes"), nodes);
    expect(writeObject(path, object), "mixed-condition fixture should be written");
    nookbit::JsonRuleSetRepository invalid(path, schema);
    const auto rejected = invalid.load();
    expect(!rejected && rejected.error().code == "rules.condition_invalid",
           "v3 node must reject fields from two condition kinds");
}

// 변환 가능한 v2만 원본을 백업하고 복합 조건은 그대로 보존하는지 검사하는 함수.
void testSafeV2Migration() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("rules.json"));
    const auto legacy = projectPath("schemas/rules-tree.schema.json");
    const auto current = projectPath("schemas/rules-tree-v3.schema.json");
    const auto defaultV2 = projectPath("resources/rules/default.tree.json");
    expect(QFile::copy(defaultV2, path), "simple v2 fixture should be copied");
    nookbit::JsonRuleSetRepository repository(path, current, legacy);
    expect(repository.load() && repository.load().value().schemaVersion == 2,
           "dual-schema repository should still read v2");
    expect(repository.migrateV2ToV3().hasValue(), "simple v2 tree should migrate");
    const auto upgraded = repository.load();
    expect(upgraded && upgraded.value().schemaVersion == 3 &&
               upgraded.value().nodes[0].atomicCondition.has_value(),
           "migration should preserve rules as v3 atomic conditions");
    expect(!QDir(directory.path()).entryList(
               {QStringLiteral("rules.json.v2-backup-*.json")}, QDir::Files).isEmpty(),
           "migration should leave a v2 backup");

    const auto complex = projectPath("resources/rules/tree.example.json");
    const auto complexPath = directory.filePath(QStringLiteral("complex.json"));
    expect(QFile::copy(complex, complexPath), "compound v2 fixture should be copied");
    QFile original(complexPath);
    expect(original.open(QIODevice::ReadOnly), "compound fixture should open");
    const auto originalBytes = original.readAll();
    original.close();
    nookbit::JsonRuleSetRepository complexRules(complexPath, current, legacy);
    const auto blocked = complexRules.migrateV2ToV3();
    expect(!blocked && blocked.error().code == "rules.migration_requires_review",
           "compound v2 condition must not be split automatically");
    expect(original.open(QIODevice::ReadOnly) && original.readAll() == originalBytes,
           "blocked migration must preserve original bytes");
}

// 읽을 수 없는 파일이나 파손된 JSON이 명확한 오류를 반환하는지 검사하는 함수.
void testMissingAndMalformedFiles() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("missing.json"));
    nookbit::JsonSettingsRepository repository(path, projectPath("schemas/settings.schema.json"));
    const auto missing = repository.load();
    expect(!missing.hasValue() && missing.error().code == "json.document.open_failed",
           "missing settings should report a file error");

    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "malformed fixture should open");
    if (file.isOpen()) {
        file.write("{not-json");
        file.close();
    }
    const auto malformed = repository.load();
    expect(!malformed.hasValue() && malformed.error().code == "json.document.parse_failed",
           "malformed settings should report a parse error");
}

// 사용자가 정한 감시 조건이 저장·복원되고 잘못된 값은 거부되는지 검사하는 함수.
void testWatchSettingsRoundTrip() {
    QTemporaryDir directory;
    expect(directory.isValid(), "watch-settings directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("watch.json"));
    nookbit::JsonWatchSettingsRepository repository(
        path, projectPath("schemas/watch.schema.json"));
    nookbit::WatchSettings settings;
    settings.folder = std::filesystem::path(directory.path().toStdWString());
    settings.enabled = true;
    settings.recursive = true;
    settings.rescanIntervalMinutes = 5;
    settings.combination = nookbit::WatchCombination::All;
    settings.conditions = {
        {nookbit::WatchConditionType::FileCount, 3},
        {nookbit::WatchConditionType::OldestAgeMinutes, 10},
    };
    settings.excludePatterns = {"*.partial", "~$*"};
    expect(repository.save(settings).hasValue(), "watch settings should save");
    const auto loaded = repository.load();
    expect(loaded.hasValue() && loaded.value().enabled && loaded.value().recursive &&
               loaded.value().rescanIntervalMinutes == 5 &&
               loaded.value().combination == nookbit::WatchCombination::All &&
               loaded.value().conditions.size() == 2 &&
               loaded.value().excludePatterns.size() == 2,
           "saved watch conditions should survive reload");
    settings.conditions.push_back(settings.conditions.front());
    expect(!repository.save(settings).hasValue(), "duplicate watch conditions must not save");
    expect(repository.load().hasValue(), "rejected save must leave the earlier file intact");

    auto object = readExample(path);
    auto conditions = object.value(QStringLiteral("conditions")).toArray();
    auto first = conditions.at(0).toObject();
    first.insert(QStringLiteral("threshold"), 0);
    conditions.replace(0, first);
    object.insert(QStringLiteral("conditions"), conditions);
    expect(writeObject(path, object), "invalid watch fixture should be written");
    const auto invalid = repository.load();
    expect(!invalid.hasValue() && invalid.error().code == "json.schema_invalid",
           "invalid saved watch threshold should be rejected");
}

} // namespace

// JSON 설정과 규칙 로더 테스트를 실행하는 함수.
int main() {
    testExampleFilesLoad();
    testInvalidSettingsAreRejected();
    testInvalidRulesAreRejected();
    testRuleTreeLoadsAndRejectsInvalidNodes();
    testRuleTreeSaveRoundTrip();
    testAtomicTreeFormat();
    testSafeV2Migration();
    testMissingAndMalformedFiles();
    testWatchSettingsRoundTrip();
    if (failures == 0) {
        std::cout << "All JSON configuration tests passed.\n";
        return 0;
    }
    std::cerr << failures << " JSON configuration assertion(s) failed.\n";
    return 1;
}
