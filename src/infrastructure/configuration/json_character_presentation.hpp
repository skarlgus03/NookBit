#pragma once
#include <nookbit/domain/character_presentation.hpp>
#include <QString>

namespace nookbit {
/// JSON을 읽고 상황과 스프라이트 설정을 검증하는 함수.
[[nodiscard]] Result<CharacterPresentation> loadCharacterPresentation(
    const QString& path, const QString& schemaPath);
} // namespace nookbit
