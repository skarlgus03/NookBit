#include "infrastructure/configuration/json_character_presentation.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"
#include <QJsonArray>
#include <utility>

namespace nookbit {
// JSON을 읽고 상황·애니메이션·스프라이트 연결을 검증하는 함수.
Result<CharacterPresentation> loadCharacterPresentation(const QString& path, const QString& schemaPath) {
    const auto loaded = loadValidatedJson(path, schemaPath);
    if (!loaded) return Result<CharacterPresentation>::failure(loaded.error());
    CharacterPresentation profile;
    for (const auto& value : loaded.value().value("sheets").toArray()) {
        const auto sheet = value.toObject();
        profile.sheets.push_back({sheet.value("id").toString().toStdString(), sheet.value("image").toString().toStdString(),
            sheet.value("frameWidth").toInt(), sheet.value("frameHeight").toInt(),
            sheet.value("columns").toInt(), sheet.value("rows").toInt()});
    }
    for (const auto& value : loaded.value().value("animations").toArray()) {
        const auto object = value.toObject();
        CharacterAnimation animation;
        animation.id = object.value("id").toString().toStdString();
        animation.sheetId = object.value("sheetId").toString().toStdString();
        animation.frameDurationMs = object.value("frameDurationMs").toInt();
        animation.loop = object.value("loop").toBool();
        for (const auto& frame : object.value("frames").toArray()) animation.frames.push_back(frame.toInt());
        profile.animations.push_back(std::move(animation));
    }
    for (const auto& value : loaded.value().value("cues").toArray()) {
        const auto cue = value.toObject();
        profile.cues.push_back({cue.value("id").toString().toStdString(), cue.value("message").toString().toStdString(),
            cue.value("animationId").toString().toStdString()});
    }
    const auto checked = validateCharacterPresentation(profile);
    if (!checked) return Result<CharacterPresentation>::failure(checked.error());
    return Result<CharacterPresentation>::success(std::move(profile));
}
} // namespace nookbit
