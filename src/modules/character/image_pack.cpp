#include <nookbit/domain/image_pack.hpp>
#include <cmath>
#include <cstdint>

namespace nookbit {
// 로더 결과나 메모리의 팩이 재생 가능한 공통 프레임 불변조건을 지키는지 확인하는 함수.
Result<void> validateImagePack(const ImagePack& pack) {
    if (pack.id.empty() || pack.name.empty() || pack.version.empty() || pack.frames.empty() ||
        !std::isfinite(pack.referenceSize.width) || !std::isfinite(pack.referenceSize.height) ||
        pack.referenceSize.width <= 0 || pack.referenceSize.height <= 0 ||
        (pack.filtering != ImagePackFiltering::Nearest && pack.filtering != ImagePackFiltering::Linear))
        return Result<void>::failure({"imagepack.metadata", "Invalid pack metadata or reference size."});
    for (const auto& [id, frame] : pack.frames) {
        const auto& region = frame.region;
        if (id.empty() || !frame.imagePath.is_absolute() || frame.imageWidth <= 0 || frame.imageHeight <= 0 ||
            region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            static_cast<std::int64_t>(region.x) + region.width > frame.imageWidth ||
            static_cast<std::int64_t>(region.y) + region.height > frame.imageHeight ||
            !std::isfinite(frame.pivot.x) || !std::isfinite(frame.pivot.y) ||
            !std::isfinite(frame.pixelsPerUnit) || frame.pixelsPerUnit <= 0 ||
            !std::isfinite(region.width / frame.pixelsPerUnit) || !std::isfinite(region.height / frame.pixelsPerUnit))
            return Result<void>::failure({"imagepack.frame", id + ": Invalid resolved frame."});
    }
    for (const auto& [id, animation] : pack.animations) {
        if (id.empty() || animation.steps.empty())
            return Result<void>::failure({"imagepack.animation", id + ": Empty animation."});
        for (const auto& step : animation.steps) {
            if (!pack.frames.contains(step.frameId) || step.durationMs < 16 || step.durationMs > 60000)
                return Result<void>::failure({"imagepack.animation", id + ": Invalid frame reference or duration."});
        }
    }
    for (const auto& [cue, animation] : pack.bindings) {
        if (cue.empty() || !pack.animations.contains(animation))
            return Result<void>::failure({"imagepack.binding", cue + ": Invalid animation binding."});
    }
    for (const auto* cue : {"resting", "calling", "working"}) {
        if (!pack.bindings.contains(cue))
            return Result<void>::failure({"imagepack.fallback_missing", std::string(cue) + ": Missing fallback binding."});
    }
    return Result<void>::success();
}
} // namespace nookbit
