#pragma once

#include <nookbit/domain/result.hpp>

#include <QJsonObject>
#include <QString>

namespace nookbit {

/// JSON 파일을 읽고 프로젝트 Schema의 지원 규칙으로 검증하는 함수.
[[nodiscard]] Result<QJsonObject> loadValidatedJson(
    const QString& documentPath,
    const QString& schemaPath);

/// 메모리의 JSON 객체를 저장 전에 같은 Schema로 검증하는 함수.
[[nodiscard]] Result<void> validateJsonObject(
    const QJsonObject& document,
    const QString& schemaPath);

} // namespace nookbit
