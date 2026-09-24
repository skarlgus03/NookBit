#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"

#include <QJsonArray>
#include <QJsonObject>

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

} // namespace nookbit
