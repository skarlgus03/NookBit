#pragma once

#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>

#include <filesystem>
#include <functional>

namespace nookbit {

using PathExists = std::function<bool(const std::filesystem::path&)>;

class CollisionResolver final {
public:
    [[nodiscard]] static Result<std::filesystem::path> resolve(
        const std::filesystem::path& requestedDestination,
        CollisionPolicy policy,
        const PathExists& exists);
};

} // namespace nookbit
