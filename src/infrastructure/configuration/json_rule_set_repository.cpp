#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <limits>
#include <set>
#include <utility>

namespace nookbit {
namespace {

// 숫자 조건을 손실 없이 파일 크기 타입으로 변환하는 함수.
Result<std::uintmax_t> byteCount(const QJsonObject& when, const QString& key) {
    const double value = when.value(key).toDouble();
    constexpr double largestExactInteger = 9007199254740991.0;
    if (value > largestExactInteger) {
        return Result<std::uintmax_t>::failure({
            "rules.byte_count_too_large", "File size must fit in an exact JSON integer.",
        });
    }
    return Result<std::uintmax_t>::success(static_cast<std::uintmax_t>(value));
}

// 나이 조건을 C++ 정수 범위 안에서 변환하는 함수.
Result<int> ageDays(const QJsonObject& when, const QString& key) {
    const double value = when.value(key).toDouble();
    if (value > static_cast<double>(std::numeric_limits<int>::max())) {
        return Result<int>::failure({
            "rules.age_too_large", "File age exceeds the supported range.",
        });
    }
    return Result<int>::success(static_cast<int>(value));
}

// 규칙 JSON 하나를 Qt에 의존하지 않는 코어 모델로 옮기는 함수.
Result<ClassificationRule> convertRule(const QJsonObject& object) {
    ClassificationRule rule;
    rule.id = object.value(QStringLiteral("id")).toString().toStdString();
    rule.enabled = object.value(QStringLiteral("enabled")).toBool();
    rule.priority = object.value(QStringLiteral("priority")).toInt();
    const auto when = object.value(QStringLiteral("when")).toObject();
    for (const auto& extension : when.value(QStringLiteral("extensions")).toArray()) {
        rule.extensions.push_back(extension.toString().toStdString());
    }
    for (const auto& glob : when.value(QStringLiteral("fileNameGlobs")).toArray()) {
        rule.fileNameGlobs.push_back(glob.toString().toStdString());
    }
    if (when.contains(QStringLiteral("minimumBytes"))) {
        auto value = byteCount(when, QStringLiteral("minimumBytes"));
        if (!value) return Result<ClassificationRule>::failure(value.error());
        rule.minimumBytes = value.value();
    }
    if (when.contains(QStringLiteral("maximumBytes"))) {
        auto value = byteCount(when, QStringLiteral("maximumBytes"));
        if (!value) return Result<ClassificationRule>::failure(value.error());
        rule.maximumBytes = value.value();
    }
    if (when.contains(QStringLiteral("minimumAgeDays"))) {
        auto value = ageDays(when, QStringLiteral("minimumAgeDays"));
        if (!value) return Result<ClassificationRule>::failure(value.error());
        rule.minimumAgeDays = value.value();
    }
    if (when.contains(QStringLiteral("maximumAgeDays"))) {
        auto value = ageDays(when, QStringLiteral("maximumAgeDays"));
        if (!value) return Result<ClassificationRule>::failure(value.error());
        rule.maximumAgeDays = value.value();
    }
    if ((rule.minimumBytes && rule.maximumBytes && *rule.minimumBytes > *rule.maximumBytes) ||
        (rule.minimumAgeDays && rule.maximumAgeDays && *rule.minimumAgeDays > *rule.maximumAgeDays)) {
        return Result<ClassificationRule>::failure({
            "rules.range_invalid", "A rule minimum cannot exceed its maximum.",
        });
    }

    const auto then = object.value(QStringLiteral("then")).toObject();
    rule.destinationRelativePath =
        std::filesystem::path(then.value(QStringLiteral("destination")).toString().toStdWString());
    const auto policy = then.value(QStringLiteral("collisionPolicy")).toString();
    if (policy == QStringLiteral("rename")) rule.collisionPolicy = CollisionPolicy::Rename;
    if (policy == QStringLiteral("skip")) rule.collisionPolicy = CollisionPolicy::Skip;
    if (policy == QStringLiteral("abort")) rule.collisionPolicy = CollisionPolicy::Abort;
    return Result<ClassificationRule>::success(std::move(rule));
}

// 목적지 JSON을 안전한 경로 판단에 사용할 모델로 옮기는 함수.
RuleDestination convertDestination(const QJsonObject& object) {
    RuleDestination destination;
    destination.relativePath = std::filesystem::path(
        object.value(QStringLiteral("path")).toString().toStdWString());
    const auto policy = object.value(QStringLiteral("collisionPolicy")).toString();
    if (policy == QStringLiteral("rename")) destination.collisionPolicy = CollisionPolicy::Rename;
    if (policy == QStringLiteral("skip")) destination.collisionPolicy = CollisionPolicy::Skip;
    if (policy == QStringLiteral("abort")) destination.collisionPolicy = CollisionPolicy::Abort;
    return destination;
}

// 트리 노드의 조건 JSON을 파일 검사 모델로 옮기는 함수.
Result<RuleCondition> convertCondition(const QJsonObject& object) {
    RuleCondition condition;
    for (const auto& extension : object.value(QStringLiteral("extensions")).toArray()) {
        condition.extensions.push_back(extension.toString().toStdString());
    }
    for (const auto& glob : object.value(QStringLiteral("fileNameGlobs")).toArray()) {
        condition.fileNameGlobs.push_back(glob.toString().toStdString());
    }
    for (const auto& key : {QStringLiteral("minimumBytes"), QStringLiteral("maximumBytes"),
                            QStringLiteral("greaterThanBytes")}) {
        if (!object.contains(key)) continue;
        auto value = byteCount(object, key);
        if (!value) return Result<RuleCondition>::failure(value.error());
        if (key == QStringLiteral("minimumBytes")) condition.minimumBytes = value.value();
        if (key == QStringLiteral("maximumBytes")) condition.maximumBytes = value.value();
        if (key == QStringLiteral("greaterThanBytes")) condition.greaterThanBytes = value.value();
    }
    for (const auto& key : {QStringLiteral("minimumAgeDays"), QStringLiteral("maximumAgeDays")}) {
        if (!object.contains(key)) continue;
        auto value = ageDays(object, key);
        if (!value) return Result<RuleCondition>::failure(value.error());
        if (key == QStringLiteral("minimumAgeDays")) condition.minimumAgeDays = value.value();
        if (key == QStringLiteral("maximumAgeDays")) condition.maximumAgeDays = value.value();
    }
    if ((condition.minimumBytes && condition.maximumBytes &&
         *condition.minimumBytes > *condition.maximumBytes) ||
        (condition.greaterThanBytes && condition.maximumBytes &&
         *condition.greaterThanBytes >= *condition.maximumBytes) ||
        (condition.minimumAgeDays && condition.maximumAgeDays &&
         *condition.minimumAgeDays > *condition.maximumAgeDays)) {
        return Result<RuleCondition>::failure({
            "rules.range_invalid", "A rule minimum cannot exceed its maximum.",
        });
    }
    return Result<RuleCondition>::success(std::move(condition));
}

// 형제 노드의 순서와 else 유일성을 검증하며 트리를 읽는 함수.
Result<std::vector<RuleNode>> convertNodes(const QJsonArray& array, std::set<std::string>& ids,
                                            int depth, int& nodeCount) {
    if (depth > 32 || nodeCount + array.size() > 1000) {
        return Result<std::vector<RuleNode>>::failure({
            "rules.tree_too_large", "Rule tree exceeds its depth or node limit.",
        });
    }
    std::vector<RuleNode> nodes;
    nodes.reserve(static_cast<std::size_t>(array.size()));
    bool foundOtherwise = false;
    for (const auto& value : array) {
        const auto object = value.toObject();
        RuleNode node;
        node.id = object.value(QStringLiteral("id")).toString().toStdString();
        node.enabled = object.value(QStringLiteral("enabled")).toBool();
        node.otherwise = object.value(QStringLiteral("otherwise")).toBool();
        if (!ids.insert(node.id).second) {
            return Result<std::vector<RuleNode>>::failure({
                "rules.duplicate_id", "Each rule node must have a unique ID.",
            });
        }
        if (node.otherwise) {
            if (foundOtherwise || object.contains(QStringLiteral("when"))) {
                return Result<std::vector<RuleNode>>::failure({
                    "rules.invalid_otherwise", "Each level allows one else node without a condition.",
                });
            }
            foundOtherwise = true;
        } else {
            if (!object.contains(QStringLiteral("when"))) {
                return Result<std::vector<RuleNode>>::failure({
                    "rules.condition_missing", "A normal rule node needs a condition.",
                });
            }
            auto condition = convertCondition(object.value(QStringLiteral("when")).toObject());
            if (!condition) return Result<std::vector<RuleNode>>::failure(condition.error());
            node.condition = std::move(condition.value());
        }
        const auto children = object.value(QStringLiteral("children")).toArray();
        if (!children.isEmpty() && object.contains(QStringLiteral("destination"))) {
            return Result<std::vector<RuleNode>>::failure({
                "rules.internal_destination", "Only a leaf node may have a destination.",
            });
        }
        if (object.contains(QStringLiteral("destination"))) {
            node.destination = convertDestination(
                object.value(QStringLiteral("destination")).toObject());
        }
        ++nodeCount;
        auto descendants = convertNodes(children, ids, depth + 1, nodeCount);
        if (!descendants) return Result<std::vector<RuleNode>>::failure(descendants.error());
        node.children = std::move(descendants.value());
        nodes.push_back(std::move(node));
    }
    return Result<std::vector<RuleNode>>::success(std::move(nodes));
}

// 리프 목적지를 상대 경로와 충돌 정책이 있는 JSON으로 변환하는 함수.
Result<QJsonObject> destinationObject(const RuleDestination& destination) {
    const auto& path = destination.relativePath;
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return Result<QJsonObject>::failure({
            "rules.destination_invalid", "Destination must be a relative path.",
        });
    }
    for (const auto& part : path) {
        if (part == L"..") {
            return Result<QJsonObject>::failure({
                "rules.destination_invalid", "Destination cannot leave the organizing root.",
            });
        }
    }
    QString policy = QStringLiteral("inherit");
    if (destination.collisionPolicy == CollisionPolicy::Rename) policy = QStringLiteral("rename");
    if (destination.collisionPolicy == CollisionPolicy::Skip) policy = QStringLiteral("skip");
    if (destination.collisionPolicy == CollisionPolicy::Abort) policy = QStringLiteral("abort");
    return Result<QJsonObject>::success(QJsonObject{
        {QStringLiteral("path"), QString::fromStdWString(path.wstring())},
        {QStringLiteral("collisionPolicy"), policy},
    });
}

// 파일 조건의 정밀도와 범위를 확인하며 JSON으로 변환하는 함수.
Result<QJsonObject> conditionObject(const RuleCondition& condition) {
    QJsonObject object;
    if (!condition.extensions.empty()) {
        QJsonArray values;
        for (const auto& value : condition.extensions) values.append(QString::fromStdString(value));
        object.insert(QStringLiteral("extensions"), values);
    }
    if (!condition.fileNameGlobs.empty()) {
        QJsonArray values;
        for (const auto& value : condition.fileNameGlobs) values.append(QString::fromStdString(value));
        object.insert(QStringLiteral("fileNameGlobs"), values);
    }
    constexpr std::uintmax_t largestExactInteger = 9007199254740991ULL;
    for (const auto& [key, value] : {
             std::pair{QStringLiteral("minimumBytes"), condition.minimumBytes},
             std::pair{QStringLiteral("maximumBytes"), condition.maximumBytes},
             std::pair{QStringLiteral("greaterThanBytes"), condition.greaterThanBytes}}) {
        if (!value) continue;
        if (*value > largestExactInteger) {
            return Result<QJsonObject>::failure({
                "rules.byte_count_too_large", "File size must fit in an exact JSON integer.",
            });
        }
        object.insert(key, static_cast<double>(*value));
    }
    if (condition.minimumAgeDays) {
        object.insert(QStringLiteral("minimumAgeDays"), *condition.minimumAgeDays);
    }
    if (condition.maximumAgeDays) {
        object.insert(QStringLiteral("maximumAgeDays"), *condition.maximumAgeDays);
    }
    if ((condition.minimumBytes && condition.maximumBytes &&
         *condition.minimumBytes > *condition.maximumBytes) ||
        (condition.greaterThanBytes && condition.maximumBytes &&
         *condition.greaterThanBytes >= *condition.maximumBytes) ||
        (condition.minimumAgeDays && condition.maximumAgeDays &&
         *condition.minimumAgeDays > *condition.maximumAgeDays)) {
        return Result<QJsonObject>::failure({
            "rules.range_invalid", "A rule minimum cannot exceed its maximum.",
        });
    }
    return Result<QJsonObject>::success(std::move(object));
}

// 노드 순서와 else 유일성을 보존하며 트리를 JSON으로 변환하는 함수.
Result<QJsonArray> nodesObject(const std::vector<RuleNode>& nodes, std::set<std::string>& ids,
                              int depth, int& nodeCount) {
    if (depth > 32 || nodeCount + nodes.size() > 1000) {
        return Result<QJsonArray>::failure({
            "rules.tree_too_large", "Rule tree exceeds its depth or node limit.",
        });
    }
    QJsonArray result;
    bool foundOtherwise = false;
    for (const auto& node : nodes) {
        if (!ids.insert(node.id).second) {
            return Result<QJsonArray>::failure({
                "rules.duplicate_id", "Each rule node must have a unique ID.",
            });
        }
        QJsonObject object{
            {QStringLiteral("id"), QString::fromStdString(node.id)},
            {QStringLiteral("enabled"), node.enabled},
        };
        if (node.otherwise) {
            if (foundOtherwise) {
                return Result<QJsonArray>::failure({
                    "rules.invalid_otherwise", "Each level allows one else node.",
                });
            }
            foundOtherwise = true;
            auto condition = conditionObject(node.condition);
            if (!condition) return Result<QJsonArray>::failure(condition.error());
            if (!condition.value().isEmpty()) {
                return Result<QJsonArray>::failure({
                    "rules.invalid_otherwise", "An else node cannot have a condition.",
                });
            }
            object.insert(QStringLiteral("otherwise"), true);
        } else {
            auto condition = conditionObject(node.condition);
            if (!condition) return Result<QJsonArray>::failure(condition.error());
            object.insert(QStringLiteral("when"), condition.value());
        }
        if (!node.children.empty() && node.destination) {
            return Result<QJsonArray>::failure({
                "rules.internal_destination", "Only a leaf node may have a destination.",
            });
        }
        if (node.destination) {
            auto destination = destinationObject(*node.destination);
            if (!destination) return Result<QJsonArray>::failure(destination.error());
            object.insert(QStringLiteral("destination"), destination.value());
        }
        ++nodeCount;
        if (!node.children.empty()) {
            auto children = nodesObject(node.children, ids, depth + 1, nodeCount);
            if (!children) return Result<QJsonArray>::failure(children.error());
            object.insert(QStringLiteral("children"), children.value());
        }
        result.append(object);
    }
    return Result<QJsonArray>::success(std::move(result));
}

} // namespace

// 규칙 파일과 Schema 경로를 보관하는 생성자.
JsonRuleSetRepository::JsonRuleSetRepository(QString rulesPath, QString schemaPath)
    : rulesPath_(std::move(rulesPath)), schemaPath_(std::move(schemaPath)) {}

// 전체 JSON 구조를 검증하고 중복 없는 규칙 묶음으로 변환하는 함수.
Result<RuleSet> JsonRuleSetRepository::load() const {
    auto loaded = loadValidatedJson(rulesPath_, schemaPath_);
    if (!loaded) return Result<RuleSet>::failure(loaded.error());

    RuleSet result;
    result.id = loaded.value().value(QStringLiteral("ruleSetId")).toString().toStdString();
    result.version = loaded.value().value(QStringLiteral("version")).toString().toStdString();
    result.schemaVersion = loaded.value().value(QStringLiteral("schemaVersion")).toInt();
    if (result.schemaVersion == 2) {
        std::set<std::string> ids;
        int nodeCount = 0;
        auto nodes = convertNodes(loaded.value().value(QStringLiteral("nodes")).toArray(),
                                  ids, 0, nodeCount);
        if (!nodes) return Result<RuleSet>::failure(nodes.error());
        result.nodes = std::move(nodes.value());
        if (loaded.value().contains(QStringLiteral("globalOtherwise"))) {
            result.globalOtherwise = convertDestination(
                loaded.value().value(QStringLiteral("globalOtherwise")).toObject());
        }
        return Result<RuleSet>::success(std::move(result));
    }
    if (result.schemaVersion != 1) {
        return Result<RuleSet>::failure({
            "rules.schema_version", "Unsupported classification rule schema version.",
        });
    }
    std::set<std::string> ids;
    for (const auto& value : loaded.value().value(QStringLiteral("rules")).toArray()) {
        auto rule = convertRule(value.toObject());
        if (!rule) return Result<RuleSet>::failure(rule.error());
        if (!ids.insert(rule.value().id).second) {
            return Result<RuleSet>::failure({
                "rules.duplicate_id", "Each classification rule must have a unique ID.",
            });
        }
        result.rules.push_back(std::move(rule.value()));
    }
    return Result<RuleSet>::success(std::move(result));
}

// v2 규칙 트리를 검사하고 기존 파일을 보호하며 JSON으로 저장하는 함수.
Result<void> JsonRuleSetRepository::save(const RuleSet& rules) const {
    if (rules.schemaVersion != 2 || !rules.rules.empty()) {
        return Result<void>::failure({
            "rules.save_version", "Only a v2 rule tree can be saved.",
        });
    }
    if (rulesPath_.startsWith(QLatin1Char(':')) || !QFileInfo(rulesPath_).isAbsolute()) {
        return Result<void>::failure({
            "rules.path_invalid", "Rule trees can only be saved to an absolute local path.",
        });
    }
    std::set<std::string> ids;
    int nodeCount = 0;
    auto nodes = nodesObject(rules.nodes, ids, 0, nodeCount);
    if (!nodes) return Result<void>::failure(nodes.error());
    QJsonObject object{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("ruleSetId"), QString::fromStdString(rules.id)},
        {QStringLiteral("version"), QString::fromStdString(rules.version)},
        {QStringLiteral("nodes"), nodes.value()},
    };
    if (rules.globalOtherwise) {
        auto destination = destinationObject(*rules.globalOtherwise);
        if (!destination) return Result<void>::failure(destination.error());
        object.insert(QStringLiteral("globalOtherwise"), destination.value());
    }
    auto checked = validateJsonObject(object, schemaPath_);
    if (!checked) return checked;
    if (!QDir().mkpath(QFileInfo(rulesPath_).absolutePath())) {
        return Result<void>::failure({
            "rules.path_invalid", "Cannot prepare the rule file location.",
        });
    }
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(rulesPath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        return Result<void>::failure({
            "rules.save_failed", "Cannot save the rule tree atomically.",
        });
    }
    return Result<void>::success();
}

} // namespace nookbit
