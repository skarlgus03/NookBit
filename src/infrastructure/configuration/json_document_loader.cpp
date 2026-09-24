#include "infrastructure/configuration/json_document_loader.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <string>

namespace nookbit {
namespace {

// Schema 오류를 위치와 함께 반환하는 함수.
Result<void> invalid(const QString& location, const QString& detail) {
    return Result<void>::failure({
        "json.schema_invalid",
        (location + QStringLiteral(": ") + detail).toStdString(),
    });
}

// Qt JSON 값이 Schema의 type 선언과 일치하는지 확인하는 함수.
bool hasType(const QJsonValue& value, const QString& type) {
    if (type == QStringLiteral("object")) return value.isObject();
    if (type == QStringLiteral("array")) return value.isArray();
    if (type == QStringLiteral("string")) return value.isString();
    if (type == QStringLiteral("boolean")) return value.isBool();
    if (type == QStringLiteral("number")) return value.isDouble();
    if (type == QStringLiteral("integer")) {
        return value.isDouble() && std::isfinite(value.toDouble()) &&
               std::trunc(value.toDouble()) == value.toDouble();
    }
    return false;
}

// Schema의 검증 규칙을 값 하나에 재귀적으로 적용하는 함수.
Result<void> validateValue(
    const QJsonValue& value,
    const QJsonObject& schema,
    const QJsonObject& schemaRoot,
    const QString& location) {
    if (schema.contains(QStringLiteral("$ref"))) {
        const auto reference = schema.value(QStringLiteral("$ref")).toString();
        if (!reference.startsWith(QStringLiteral("#/$defs/"))) {
            return invalid(location, QStringLiteral("unsupported Schema reference"));
        }
        const auto name = reference.mid(8);
        const auto definition = schemaRoot.value(QStringLiteral("$defs"))
                                    .toObject().value(name);
        if (!definition.isObject()) {
            return invalid(location, QStringLiteral("missing Schema definition"));
        }
        return validateValue(value, definition.toObject(), schemaRoot, location);
    }

    if (schema.contains(QStringLiteral("type")) &&
        !hasType(value, schema.value(QStringLiteral("type")).toString())) {
        return invalid(location, QStringLiteral("incorrect JSON type"));
    }
    if (schema.contains(QStringLiteral("const")) &&
        value != schema.value(QStringLiteral("const"))) {
        return invalid(location, QStringLiteral("required constant differs"));
    }
    if (schema.contains(QStringLiteral("enum"))) {
        bool found = false;
        for (const auto& allowed : schema.value(QStringLiteral("enum")).toArray()) {
            if (value == allowed) { found = true; break; }
        }
        if (!found) return invalid(location, QStringLiteral("value is not allowed"));
    }
    if (schema.contains(QStringLiteral("not")) &&
        validateValue(value, schema.value(QStringLiteral("not")).toObject(),
                      schemaRoot, location)) {
        return invalid(location, QStringLiteral("value matches a forbidden pattern"));
    }

    if (value.isObject()) {
        const auto object = value.toObject();
        const auto required = schema.value(QStringLiteral("required")).toArray();
        for (const auto& key : required) {
            if (!object.contains(key.toString())) {
                return invalid(location + QStringLiteral(".") + key.toString(),
                               QStringLiteral("required field is missing"));
            }
        }
        if (schema.contains(QStringLiteral("minProperties")) &&
            object.size() < schema.value(QStringLiteral("minProperties")).toInt()) {
            return invalid(location, QStringLiteral("too few properties"));
        }
        const auto properties = schema.value(QStringLiteral("properties")).toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!properties.contains(it.key())) {
                if (schema.value(QStringLiteral("additionalProperties")).toBool(true)) {
                    continue;
                }
                return invalid(location + QStringLiteral(".") + it.key(),
                               QStringLiteral("unknown field"));
            }
            auto checked = validateValue(it.value(), properties.value(it.key()).toObject(),
                                         schemaRoot, location + QStringLiteral(".") + it.key());
            if (!checked) return checked;
        }
    }

    if (value.isArray()) {
        const auto array = value.toArray();
        if (schema.contains(QStringLiteral("minItems")) &&
            array.size() < schema.value(QStringLiteral("minItems")).toInt()) {
            return invalid(location, QStringLiteral("too few items"));
        }
        if (schema.value(QStringLiteral("uniqueItems")).toBool()) {
            for (int i = 0; i < array.size(); ++i) {
                for (int j = i + 1; j < array.size(); ++j) {
                    if (array.at(i) == array.at(j)) {
                        return invalid(location, QStringLiteral("duplicate array item"));
                    }
                }
            }
        }
        if (schema.contains(QStringLiteral("items"))) {
            const auto itemSchema = schema.value(QStringLiteral("items")).toObject();
            for (int i = 0; i < array.size(); ++i) {
                auto checked = validateValue(
                    array.at(i), itemSchema, schemaRoot,
                    location + QStringLiteral("[") + QString::number(i) + QStringLiteral("]"));
                if (!checked) return checked;
            }
        }
    }

    if (value.isString()) {
        const auto string = value.toString();
        if (schema.contains(QStringLiteral("minLength")) &&
            string.size() < schema.value(QStringLiteral("minLength")).toInt()) {
            return invalid(location, QStringLiteral("string is too short"));
        }
        if (schema.contains(QStringLiteral("pattern"))) {
            const QRegularExpression pattern(schema.value(QStringLiteral("pattern")).toString());
            if (!pattern.isValid() || !pattern.match(string).hasMatch()) {
                return invalid(location, QStringLiteral("string does not match pattern"));
            }
        }
        if (schema.value(QStringLiteral("format")).toString() == QStringLiteral("date-time")) {
            const QRegularExpression timezone(QStringLiteral("(?:Z|[+-][0-9]{2}:[0-9]{2})$"));
            if (!timezone.match(string).hasMatch() ||
                !QDateTime::fromString(string, Qt::ISODateWithMs).isValid()) {
                return invalid(location, QStringLiteral("invalid date-time"));
            }
        }
    }

    if (value.isDouble()) {
        const double number = value.toDouble();
        if (schema.contains(QStringLiteral("minimum")) &&
            number < schema.value(QStringLiteral("minimum")).toDouble()) {
            return invalid(location, QStringLiteral("number is below minimum"));
        }
        if (schema.contains(QStringLiteral("maximum")) &&
            number > schema.value(QStringLiteral("maximum")).toDouble()) {
            return invalid(location, QStringLiteral("number is above maximum"));
        }
    }
    return Result<void>::success();
}

// 파일 하나를 열어 JSON 객체로 변환하는 함수.
Result<QJsonObject> readObject(const QString& path, const std::string& role) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QJsonObject>::failure({
            role + ".open_failed", "Cannot open JSON file: " + path.toStdString(),
        });
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Result<QJsonObject>::failure({
            role + ".parse_failed", "Invalid JSON object: " + path.toStdString(),
        });
    }
    return Result<QJsonObject>::success(document.object());
}

} // namespace

// JSON 문서와 Schema를 읽어 검증된 객체를 반환하는 함수.
Result<QJsonObject> loadValidatedJson(
    const QString& documentPath,
    const QString& schemaPath) {
    auto document = readObject(documentPath, "json.document");
    if (!document) return document;
    auto schema = readObject(schemaPath, "json.schema");
    if (!schema) return schema;
    auto checked = validateValue(document.value(), schema.value(), schema.value(),
                                 QStringLiteral("$"));
    if (!checked) return Result<QJsonObject>::failure(checked.error());
    return document;
}

} // namespace nookbit
