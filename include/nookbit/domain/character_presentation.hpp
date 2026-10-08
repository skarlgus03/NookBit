#pragma once
#include <nookbit/domain/result.hpp>
#include <string>
#include <vector>

namespace nookbit {

struct CharacterSpriteSheet {
    std::string id;
    std::string image;
    int frameWidth{0}, frameHeight{0}, columns{0}, rows{0};
};
struct CharacterAnimation {
    std::string id;
    std::string sheetId;
    std::vector<int> frames;
    int frameDurationMs{100};
    bool loop{true};
};
struct CharacterCue {
    std::string id;
    std::string message;
    std::string animationId;
};
/// 상황 이름·문구·애니메이션·격자 이미지의 연결을 표현하는 Qt 독립 설정.
struct CharacterPresentation {
    std::vector<CharacterSpriteSheet> sheets;
    std::vector<CharacterAnimation> animations;
    std::vector<CharacterCue> cues;
};
/// 중복 이름·이미지 경로·프레임 범위·상호 참조를 검증하는 함수.
[[nodiscard]] Result<void> validateCharacterPresentation(const CharacterPresentation& presentation);

} // namespace nookbit
