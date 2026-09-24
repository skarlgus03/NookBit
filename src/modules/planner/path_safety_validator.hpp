#pragma once

#include <nookbit/domain/result.hpp>

#include <filesystem>
#include <vector>

namespace nookbit {

class PathSafetyValidator final {
public:
    [[nodiscard]] static Result<void> validateMove(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const std::vector<std::filesystem::path>& selectedFiles,
        const std::filesystem::path& homeRoot,
        const std::filesystem::path& appDataRoot);

    [[nodiscard]] static bool isWithin(
        const std::filesystem::path& child,
        const std::filesystem::path& parent);
};

} // namespace nookbit
