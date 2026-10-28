#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

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
    destination.absolute = object.value(QStringLiteral("base")).toString() == QStringLiteral("absolute");
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

// 이전 형식의 조건 필드가 모두 비어 있는지 검사하는 함수.
bool legacyConditionEmpty(const RuleCondition& condition) {
    return condition.extensions.empty() && condition.fileNameGlobs.empty() &&
           !condition.minimumBytes && !condition.maximumBytes &&
           !condition.greaterThanBytes && !condition.minimumAgeDays &&
           !condition.maximumAgeDays;
}

// v3 JSON의 조건 종류별 입력을 정확히 하나의 조건 모델로 변환하는 함수.
Result<AtomicRuleCondition> convertAtomicCondition(const QJsonObject& object) {
    const auto type = object.value(QStringLiteral("type")).toString();
    const auto comparison = object.value(QStringLiteral("comparison")).toString();
    if (type == QStringLiteral("itemType") && object.size() == 2) {
        const auto kind = object.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("file") || kind == QStringLiteral("folder"))
            return Result<AtomicRuleCondition>::success(ItemTypePredicate{
                kind == QStringLiteral("folder") ? ItemType::Folder : ItemType::File});
    }
    if (type == QStringLiteral("extension") && object.size() == 2 &&
        object.contains(QStringLiteral("values"))) {
        ExtensionPredicate condition;
        for (const auto& value : object.value(QStringLiteral("values")).toArray())
            condition.values.push_back(value.toString().toLower().toStdString());
        return Result<AtomicRuleCondition>::success(std::move(condition));
    }
    if (type == QStringLiteral("nameContains") && object.size() == 2 &&
        object.contains(QStringLiteral("text"))) {
        return Result<AtomicRuleCondition>::success(NameContainsPredicate{
            object.value(QStringLiteral("text")).toString().toStdString()});
    }
    if (type == QStringLiteral("size") && object.size() == 3 &&
        object.contains(QStringLiteral("bytes"))) {
        auto bytes = byteCount(object, QStringLiteral("bytes"));
        if (!bytes) return Result<AtomicRuleCondition>::failure(bytes.error());
        SizeComparison mode;
        if (comparison == QStringLiteral("greaterThan")) mode = SizeComparison::GreaterThan;
        else if (comparison == QStringLiteral("atLeast")) mode = SizeComparison::AtLeast;
        else if (comparison == QStringLiteral("lessThan")) mode = SizeComparison::LessThan;
        else if (comparison == QStringLiteral("atMost")) mode = SizeComparison::AtMost;
        else return Result<AtomicRuleCondition>::failure({
            "rules.condition_invalid", "Unsupported size comparison."});
        return Result<AtomicRuleCondition>::success(SizePredicate{mode, bytes.value()});
    }
    if (type == QStringLiteral("modifiedAgeDays") && object.size() == 3 &&
        object.contains(QStringLiteral("days"))) {
        auto days = ageDays(object, QStringLiteral("days"));
        if (!days) return Result<AtomicRuleCondition>::failure(days.error());
        AgeComparison mode;
        if (comparison == QStringLiteral("atLeast")) mode = AgeComparison::AtLeast;
        else if (comparison == QStringLiteral("lessThan")) mode = AgeComparison::LessThan;
        else return Result<AtomicRuleCondition>::failure({
            "rules.condition_invalid", "Unsupported age comparison."});
        return Result<AtomicRuleCondition>::success(ModifiedAgePredicate{mode, days.value()});
    }
    return Result<AtomicRuleCondition>::failure({
        "rules.condition_invalid", "A node must contain exactly one condition kind and its input."});
}

// 의미 검증 오류에 JSON 노드 경로를 붙여 문제 위치를 보여주는 함수.
Error atRuleLocation(const QString& location, Error error) {
    error.message = (location + QStringLiteral(": ") +
                     QString::fromStdString(error.message)).toStdString();
    return error;
}

// 조건·형제·리프 제약을 확인하며 노드 위치가 포함된 오류를 반환하는 함수.
Result<std::vector<RuleNode>> convertAtomicNodes(const QJsonArray& array,
                                                  std::set<std::string>& ids,
                                                  int depth, int& nodeCount,
                                                  const QString& location) {
    if (depth > 32 || nodeCount + array.size() > 1000) return Result<std::vector<RuleNode>>::failure({
        "rules.tree_too_large",
        (location + QStringLiteral(": Rule tree exceeds its depth or node limit.")).toStdString()});
    std::vector<RuleNode> nodes;
    bool foundOtherwise = false;
    for (int index = 0; index < array.size(); ++index) {
        const auto entry = array.at(index);
        const QString nodePath = location + QLatin1Char('[') +
                                 QString::number(index) + QLatin1Char(']');
        const auto object = entry.toObject();
        RuleNode node;
        node.id = object.value(QStringLiteral("id")).toString().toStdString();
        node.enabled = object.value(QStringLiteral("enabled")).toBool();
        node.otherwise = object.value(QStringLiteral("otherwise")).toBool();
        if (!ids.insert(node.id).second) return Result<std::vector<RuleNode>>::failure(
            atRuleLocation(nodePath + QStringLiteral(".id"),
                           {"rules.duplicate_id", "Duplicate rule ID: " + node.id}));
        if (node.otherwise) {
            if (foundOtherwise || object.contains(QStringLiteral("when")))
                return Result<std::vector<RuleNode>>::failure(atRuleLocation(
                    nodePath + QStringLiteral(".otherwise"),
                    {"rules.invalid_otherwise", "Each level allows one else node without a condition."}));
            foundOtherwise = true;
        } else {
            if (!object.contains(QStringLiteral("when")))
                return Result<std::vector<RuleNode>>::failure(atRuleLocation(
                    nodePath + QStringLiteral(".when"),
                    {"rules.condition_missing", "A normal rule node needs one condition."}));
            auto condition = convertAtomicCondition(object.value(QStringLiteral("when")).toObject());
            if (!condition) return Result<std::vector<RuleNode>>::failure(
                atRuleLocation(nodePath + QStringLiteral(".when"), condition.error()));
            node.atomicCondition = std::move(condition.value());
        }
        const auto children = object.value(QStringLiteral("children")).toArray();
        if (!children.isEmpty() && object.contains(QStringLiteral("destination")))
            return Result<std::vector<RuleNode>>::failure(atRuleLocation(
                nodePath + QStringLiteral(".destination"),
                {"rules.internal_destination", "Only a leaf node may have a destination."}));
        if (object.contains(QStringLiteral("exclude")) &&
            (object.contains(QStringLiteral("destination")) || !children.isEmpty()))
            return Result<std::vector<RuleNode>>::failure(atRuleLocation(
                nodePath + QStringLiteral(".exclude"),
                {"rules.invalid_exclude", "An excluded leaf cannot have a destination or children."}));
        node.exclude = object.value(QStringLiteral("exclude")).toBool();
        if (object.contains(QStringLiteral("destination")))
            node.destination = convertDestination(object.value(QStringLiteral("destination")).toObject());
        ++nodeCount;
        auto descendants = convertAtomicNodes(children, ids, depth + 1, nodeCount,
                                               nodePath + QStringLiteral(".children"));
        if (!descendants) return Result<std::vector<RuleNode>>::failure(descendants.error());
        node.children = std::move(descendants.value());
        nodes.push_back(std::move(node));
    }
    return Result<std::vector<RuleNode>>::success(std::move(nodes));
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
    if (path.empty() || (destination.absolute ? !path.is_absolute() : path.has_root_path())) {
        return Result<QJsonObject>::failure({
            "rules.destination_invalid", "Destination path does not match its declared base.",
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
    QJsonObject object{
        {QStringLiteral("path"), QString::fromStdWString(path.wstring())},
        {QStringLiteral("collisionPolicy"), policy},
    };
    if (destination.absolute) object.insert(QStringLiteral("base"), QStringLiteral("absolute"));
    return Result<QJsonObject>::success(object);
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

// v3 단일 조건 모델을 종류별 JSON 필드로 변환하고 입력값을 검사하는 함수.
Result<QJsonObject> atomicConditionObject(const AtomicRuleCondition& condition) {
    if (const auto* type = std::get_if<ItemTypePredicate>(&condition))
        return Result<QJsonObject>::success(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("itemType")},
            {QStringLiteral("kind"), type->value == ItemType::Folder ? QStringLiteral("folder") : QStringLiteral("file")}});
    if (const auto* extension = std::get_if<ExtensionPredicate>(&condition)) {
        if (extension->values.empty()) return Result<QJsonObject>::failure({
            "rules.condition_invalid", "Extension condition needs a value."});
        QJsonArray values;
        for (const auto& value : extension->values)
            values.append(QString::fromStdString(value));
        return Result<QJsonObject>::success(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("extension")},
            {QStringLiteral("values"), values}});
    }
    if (const auto* name = std::get_if<NameContainsPredicate>(&condition)) {
        if (name->text.empty()) return Result<QJsonObject>::failure({
            "rules.condition_invalid", "Name condition needs text."});
        return Result<QJsonObject>::success(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("nameContains")},
            {QStringLiteral("text"), QString::fromStdString(name->text)}});
    }
    if (const auto* size = std::get_if<SizePredicate>(&condition)) {
        constexpr std::uintmax_t largestExactInteger = 9007199254740991ULL;
        if (size->bytes > largestExactInteger) return Result<QJsonObject>::failure({
            "rules.byte_count_too_large", "File size must fit in an exact JSON integer."});
        QString comparison;
        switch (size->comparison) {
        case SizeComparison::GreaterThan: comparison = QStringLiteral("greaterThan"); break;
        case SizeComparison::AtLeast: comparison = QStringLiteral("atLeast"); break;
        case SizeComparison::LessThan: comparison = QStringLiteral("lessThan"); break;
        case SizeComparison::AtMost: comparison = QStringLiteral("atMost"); break;
        }
        return Result<QJsonObject>::success(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("size")},
            {QStringLiteral("comparison"), comparison},
            {QStringLiteral("bytes"), static_cast<double>(size->bytes)}});
    }
    const auto& age = std::get<ModifiedAgePredicate>(condition);
    if (age.days < 0) return Result<QJsonObject>::failure({
        "rules.condition_invalid", "Age cannot be negative."});
    return Result<QJsonObject>::success(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("modifiedAgeDays")},
        {QStringLiteral("comparison"), age.comparison == AgeComparison::AtLeast
            ? QStringLiteral("atLeast") : QStringLiteral("lessThan")},
        {QStringLiteral("days"), age.days}});
}

// v3 트리를 한 노드당 조건 하나인 JSON 배열로 변환하는 함수.
Result<QJsonArray> atomicNodesObject(const std::vector<RuleNode>& nodes,
                                     std::set<std::string>& ids,
                                     int depth, int& nodeCount) {
    if (depth > 32 || nodeCount + nodes.size() > 1000) return Result<QJsonArray>::failure({
        "rules.tree_too_large", "Rule tree exceeds its depth or node limit."});
    QJsonArray result;
    bool foundOtherwise = false;
    for (const auto& node : nodes) {
        if (!legacyConditionEmpty(node.condition)) return Result<QJsonArray>::failure({
            "rules.condition_invalid", "A v3 node cannot contain v2 condition fields."});
        if (!ids.insert(node.id).second) return Result<QJsonArray>::failure({
            "rules.duplicate_id", "Each rule node must have a unique ID."});
        QJsonObject object{
            {QStringLiteral("id"), QString::fromStdString(node.id)},
            {QStringLiteral("enabled"), node.enabled}};
        if (node.otherwise) {
            if (foundOtherwise || node.atomicCondition) return Result<QJsonArray>::failure({
                "rules.invalid_otherwise", "Else node cannot have a condition."});
            foundOtherwise = true;
            object.insert(QStringLiteral("otherwise"), true);
        } else {
            if (!node.atomicCondition) return Result<QJsonArray>::failure({
                "rules.condition_missing", "A normal rule node needs one condition."});
            auto condition = atomicConditionObject(*node.atomicCondition);
            if (!condition) return Result<QJsonArray>::failure(condition.error());
            object.insert(QStringLiteral("when"), condition.value());
        }
        if (!node.children.empty() && node.destination) return Result<QJsonArray>::failure({
            "rules.internal_destination", "Only a leaf node may have a destination."});
        if (node.exclude && (node.destination || !node.children.empty()))
            return Result<QJsonArray>::failure({
                "rules.invalid_exclude", "An excluded leaf cannot have a destination or children."});
        if (node.exclude) object.insert(QStringLiteral("exclude"), true);
        if (node.destination) {
            auto destination = destinationObject(*node.destination);
            if (!destination) return Result<QJsonArray>::failure(destination.error());
            object.insert(QStringLiteral("destination"), destination.value());
        }
        ++nodeCount;
        if (!node.children.empty()) {
            auto children = atomicNodesObject(node.children, ids, depth + 1, nodeCount);
            if (!children) return Result<QJsonArray>::failure(children.error());
            object.insert(QStringLiteral("children"), children.value());
        }
        result.append(object);
    }
    return Result<QJsonArray>::success(std::move(result));
}

// 단일 조건인 v2 노드만 분류 의미를 유지한 채 v3 노드로 옮기는 함수.
Result<std::vector<RuleNode>> migrateSimpleNodes(const std::vector<RuleNode>& source) {
    std::vector<RuleNode> converted;
    converted.reserve(source.size());
    for (const auto& old : source) {
        RuleNode node = old;
        node.condition = {};
        if (!old.otherwise) {
            const auto& condition = old.condition;
            const int count = static_cast<int>(!condition.extensions.empty()) +
                              static_cast<int>(!condition.fileNameGlobs.empty()) +
                              static_cast<int>(condition.minimumBytes.has_value()) +
                              static_cast<int>(condition.maximumBytes.has_value()) +
                              static_cast<int>(condition.greaterThanBytes.has_value()) +
                              static_cast<int>(condition.minimumAgeDays.has_value()) +
                              static_cast<int>(condition.maximumAgeDays.has_value());
            if (count != 1) return Result<std::vector<RuleNode>>::failure({
                "rules.migration_requires_review",
                "A v2 node combines conditions; automatic splitting would change sibling fallback behavior."});
            if (!condition.extensions.empty())
                node.atomicCondition = ExtensionPredicate{condition.extensions};
            else if (condition.minimumBytes)
                node.atomicCondition = SizePredicate{SizeComparison::AtLeast,
                                                      *condition.minimumBytes};
            else if (condition.maximumBytes)
                node.atomicCondition = SizePredicate{SizeComparison::AtMost,
                                                      *condition.maximumBytes};
            else if (condition.greaterThanBytes)
                node.atomicCondition = SizePredicate{SizeComparison::GreaterThan,
                                                      *condition.greaterThanBytes};
            else if (condition.minimumAgeDays)
                node.atomicCondition = ModifiedAgePredicate{AgeComparison::AtLeast,
                                                              *condition.minimumAgeDays};
            else return Result<std::vector<RuleNode>>::failure({
                "rules.migration_requires_review",
                "A v2 filename pattern or maximum age cannot be converted without changing its meaning."});
        }
        auto children = migrateSimpleNodes(old.children);
        if (!children) return Result<std::vector<RuleNode>>::failure(children.error());
        node.children = std::move(children.value());
        converted.push_back(std::move(node));
    }
    return Result<std::vector<RuleNode>>::success(std::move(converted));
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
        if (node.atomicCondition) return Result<QJsonArray>::failure({
            "rules.condition_invalid", "A v2 node cannot contain a v3 condition."});
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
    : JsonRuleSetRepository(std::move(rulesPath), std::move(schemaPath), {}) {}

// v2 규칙을 읽을 수 있도록 별도 Schema까지 보관하는 생성자.
JsonRuleSetRepository::JsonRuleSetRepository(QString rulesPath, QString schemaPath,
                                             QString legacySchemaPath)
    : rulesPath_(std::move(rulesPath)), schemaPath_(std::move(schemaPath)),
      legacySchemaPath_(std::move(legacySchemaPath)) {}

// 전체 JSON 구조를 검증하고 중복 없는 규칙 묶음으로 변환하는 함수.
Result<RuleSet> JsonRuleSetRepository::load() const {
    QString schema = schemaPath_;
    if (!legacySchemaPath_.isEmpty()) {
        QFile raw(rulesPath_);
        if (!raw.open(QIODevice::ReadOnly)) return Result<RuleSet>::failure({
            "json.document.open_failed", "Cannot open rule file."});
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(raw.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return Result<RuleSet>::failure({
                "json.document.parse_failed", "Invalid rule JSON object."});
        if (document.object().value(QStringLiteral("schemaVersion")).toInt() == 2)
            schema = legacySchemaPath_;
    }
    auto loaded = loadValidatedJson(rulesPath_, schema);
    if (!loaded) return Result<RuleSet>::failure(loaded.error());

    RuleSet result;
    result.id = loaded.value().value(QStringLiteral("ruleSetId")).toString().toStdString();
    result.version = loaded.value().value(QStringLiteral("version")).toString().toStdString();
    result.schemaVersion = loaded.value().value(QStringLiteral("schemaVersion")).toInt();
    if (result.schemaVersion == 2 || result.schemaVersion == 3) {
        std::set<std::string> ids;
        int nodeCount = 0;
        auto nodes = result.schemaVersion == 3
            ? convertAtomicNodes(loaded.value().value(QStringLiteral("nodes")).toArray(),
                                 ids, 0, nodeCount, QStringLiteral("$.nodes"))
            : convertNodes(loaded.value().value(QStringLiteral("nodes")).toArray(),
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

// 외부 규칙 파일을 별도의 읽기 전용 저장소로 열어 검증하는 함수.
Result<RuleSet> JsonRuleSetRepository::loadFile(const std::filesystem::path& path) const {
    JsonRuleSetRepository imported(QString::fromStdWString(path.wstring()), schemaPath_);
    return imported.load();
}

// 활성 규칙 파일과 같은 대상을 거부하고 기존 검증·원자적 저장 절차를 재사용하는 함수.
Result<void> JsonRuleSetRepository::saveFile(const std::filesystem::path& path,
                                             const RuleSet& rules) const {
    const QFileInfo target(QString::fromStdWString(path.wstring()));
    const QFileInfo active(rulesPath_);
    if (!target.isAbsolute() || target.isDir()) return Result<void>::failure({
        "rules.export_path_invalid", "Export requires an absolute local file path."});
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    const auto sameName = QDir::cleanPath(target.absoluteFilePath()).compare(
        QDir::cleanPath(active.absoluteFilePath()), pathCase) == 0;
    const auto sameExistingFile = target.exists() && active.exists() &&
        target.canonicalFilePath().compare(active.canonicalFilePath(), pathCase) == 0;
    if (sameName || sameExistingFile) return Result<void>::failure({
        "rules.export_active_path", "Cannot export over the active rule file."});
    JsonRuleSetRepository exported(target.absoluteFilePath(), schemaPath_, legacySchemaPath_);
    return exported.save(rules);
}

// v2 또는 v3 규칙 트리를 검사하고 기존 파일을 보호하며 JSON으로 저장하는 함수.
Result<void> JsonRuleSetRepository::save(const RuleSet& rules) const {
    if ((rules.schemaVersion != 2 && rules.schemaVersion != 3) || !rules.rules.empty()) {
        return Result<void>::failure({
            "rules.save_version", "Only a v2 or v3 rule tree can be saved.",
        });
    }
    if (rulesPath_.startsWith(QLatin1Char(':')) || !QFileInfo(rulesPath_).isAbsolute()) {
        return Result<void>::failure({
            "rules.path_invalid", "Rule trees can only be saved to an absolute local path.",
        });
    }
    std::set<std::string> ids;
    int nodeCount = 0;
    auto nodes = rules.schemaVersion == 3
        ? atomicNodesObject(rules.nodes, ids, 0, nodeCount)
        : nodesObject(rules.nodes, ids, 0, nodeCount);
    if (!nodes) return Result<void>::failure(nodes.error());
    QJsonObject object{
        {QStringLiteral("schemaVersion"), rules.schemaVersion},
        {QStringLiteral("ruleSetId"), QString::fromStdString(rules.id)},
        {QStringLiteral("version"), QString::fromStdString(rules.version)},
        {QStringLiteral("nodes"), nodes.value()},
    };
    if (rules.globalOtherwise) {
        auto destination = destinationObject(*rules.globalOtherwise);
        if (!destination) return Result<void>::failure(destination.error());
        object.insert(QStringLiteral("globalOtherwise"), destination.value());
    }
    const auto& schema = rules.schemaVersion == 2 && !legacySchemaPath_.isEmpty()
        ? legacySchemaPath_ : schemaPath_;
    auto checked = validateJsonObject(object, schema);
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

// 의미가 보존되는 단일 조건 v2 규칙만 원본 백업 후 v3 파일로 저장하는 함수.
Result<void> JsonRuleSetRepository::migrateV2ToV3() const {
    if (legacySchemaPath_.isEmpty() || rulesPath_.startsWith(QLatin1Char(':')) ||
        !QFileInfo(rulesPath_).isAbsolute()) return Result<void>::failure({
        "rules.migration_unavailable", "Migration requires a local rule file and both schemas."});
    const auto current = load();
    if (!current) return Result<void>::failure(current.error());
    if (current.value().schemaVersion != 2) return Result<void>::failure({
        "rules.migration_unavailable", "Only v2 rule trees can be migrated."});
    auto convertedNodes = migrateSimpleNodes(current.value().nodes);
    if (!convertedNodes) return Result<void>::failure(convertedNodes.error());
    RuleSet upgraded = current.value();
    upgraded.schemaVersion = 3;
    upgraded.nodes = std::move(convertedNodes.value());
    upgraded.version = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    const auto backupPath = rulesPath_ + QStringLiteral(".v2-backup-") +
                            QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".json");
    if (!QFile::copy(rulesPath_, backupPath)) return Result<void>::failure({
        "rules.backup_failed", "Cannot back up the v2 rule file; migration was cancelled."});
    return save(upgraded);
}

} // namespace nookbit
