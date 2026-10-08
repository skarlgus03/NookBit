#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <nookbit/domain/result.hpp>

namespace nookbit {
struct ImagePackPoint { double x{0}, y{0}; };
struct ImagePackSize { double width{0}, height{0}; };
struct ImagePackRect { int x{0}, y{0}, width{0}, height{0}; };
enum class ImagePackFiltering { Nearest, Linear };
/// PNG·격자·직사각형 영역을 같은 형태로 확정한 프레임. pivot은 영역 내부의 논리 좌표다.
struct ImagePackFrame {
    std::filesystem::path imagePath;
    int imageWidth{0}, imageHeight{0};
    ImagePackRect region;
    ImagePackPoint pivot;
    double pixelsPerUnit{1};
};
struct ImagePackStep { std::string frameId; int durationMs{100}; };
struct ImagePackAnimation { bool loop{true}; std::vector<ImagePackStep> steps; };
/// 이미지 픽셀이나 개인 배율을 보관하지 않는 검증된 ImagePack 데이터.
struct ImagePack {
    std::string id, name, version, author;
    ImagePackSize referenceSize;
    ImagePackFiltering filtering{ImagePackFiltering::Nearest};
    std::map<std::string, ImagePackFrame> frames;
    std::map<std::string, ImagePackAnimation> animations;
    std::map<std::string, std::string> bindings;
};
/// 로더 결과나 메모리의 팩이 재생 가능한 공통 프레임 불변조건을 지키는지 확인하는 함수.
[[nodiscard]] Result<void> validateImagePack(const ImagePack& pack);
} // namespace nookbit
