#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_settings_repository.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QFile>
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
    testMissingAndMalformedFiles();
    testWatchSettingsRoundTrip();
    if (failures == 0) {
        std::cout << "All JSON configuration tests passed.\n";
        return 0;
    }
    std::cerr << failures << " JSON configuration assertion(s) failed.\n";
    return 1;
}
