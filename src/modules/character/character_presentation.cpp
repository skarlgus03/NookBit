#include <nookbit/domain/character_presentation.hpp>
#include <algorithm>
#include <map>
#include <set>

namespace nookbit {
namespace {
// 상황·애니메이션 이름을 설정에서 일관된 ASCII 식별자로 사용할 수 있는지 확인하는 함수.
bool validIdentifier(const std::string& id) {
    const auto alphanumeric = [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9');
    };
    return !id.empty() && alphanumeric(static_cast<unsigned char>(id.front())) &&
        std::all_of(id.begin(), id.end(), [&alphanumeric](unsigned char value) {
            return alphanumeric(value) || value == '.' || value == '-' || value == '_';
        });
}
} // namespace
// 중복 이름·이미지 경로·프레임 범위·상호 참조를 검증하는 함수.
Result<void> validateCharacterPresentation(const CharacterPresentation& presentation) {
    std::map<std::string, const CharacterSpriteSheet*> sheets;
    for (const auto& sheet : presentation.sheets) {
        if (!validIdentifier(sheet.id) || !sheets.emplace(sheet.id, &sheet).second)
            return Result<void>::failure({"character.sheet_id", "Sprite sheet IDs must be nonempty and unique."});
        if (sheet.frameWidth < 1 || sheet.frameWidth > 4096 || sheet.frameHeight < 1 || sheet.frameHeight > 4096 ||
            sheet.columns < 1 || sheet.columns > 1024 || sheet.rows < 1 || sheet.rows > 1024)
            return Result<void>::failure({"character.sheet_grid", "Invalid sprite grid: " + sheet.id});
        if (sheet.image.empty() || sheet.image.find('\0') != std::string::npos ||
            sheet.image.find(':') != std::string::npos || sheet.image.find('\\') != std::string::npos ||
            sheet.image.starts_with('/') || !sheet.image.ends_with(".png") ||
            sheet.image.starts_with("../") || sheet.image.find("/../") != std::string::npos)
            return Result<void>::failure({"character.sheet_path", "Use a relative PNG path without parent traversal: " + sheet.id});
    }
    std::set<std::string> animations;
    for (const auto& animation : presentation.animations) {
        if (!validIdentifier(animation.id) || !animations.insert(animation.id).second)
            return Result<void>::failure({"character.animation_id", "Animation IDs must be nonempty and unique."});
        const auto sheet = sheets.find(animation.sheetId);
        if (sheet == sheets.end())
            return Result<void>::failure({"character.animation_sheet", "Unknown sheet for animation: " + animation.id});
        if (animation.frames.empty() || animation.frameDurationMs < 16 || animation.frameDurationMs > 60000)
            return Result<void>::failure({"character.animation_timing", "Invalid frames or timing: " + animation.id});
        const auto cells = sheet->second->columns * sheet->second->rows;
        if (std::any_of(animation.frames.begin(), animation.frames.end(), [cells](int frame) { return frame < 0 || frame >= cells; }))
            return Result<void>::failure({"character.animation_frame", "Frame outside sheet grid: " + animation.id});
    }
    std::set<std::string> cues;
    for (const auto& cue : presentation.cues) {
        if (!validIdentifier(cue.id) || !cues.insert(cue.id).second)
            return Result<void>::failure({"character.cue_id", "Cue IDs must be nonempty and unique."});
        if (!cue.animationId.empty() && !animations.contains(cue.animationId))
            return Result<void>::failure({"character.cue_animation", "Unknown animation for cue: " + cue.id});
    }
    for (const auto* required : {"resting", "calling", "working"}) {
        if (!cues.contains(required))
            return Result<void>::failure({"character.cue_missing", std::string("Required fallback cue: ") + required});
    }
    return Result<void>::success();
}
} // namespace nookbit
