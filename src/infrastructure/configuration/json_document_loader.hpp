#pragma once

#include <nookbit/domain/result.hpp>

#include <QJsonObject>
#include <QString>

namespace nookbit {

/// JSON 파일을 읽고 프로젝트 Schema의 지원 규칙으로 검증하는 함수.
[[nodiscard]] Result<QJsonObject> loadValidatedJson(
    const QString& documentPath,
    const QString& schemaPath);

} // namespace nookbit
