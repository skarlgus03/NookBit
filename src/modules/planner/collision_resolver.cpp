#include "modules/planner/collision_resolver.hpp"

#include <string>

namespace nookbit {

namespace {

constexpr int maximumRenameAttempts = 10'000;

std::filesystem::path renamedCandidate(
    const std::filesystem::path& requested,
    int index) {
    const auto parent = requested.parent_path();
    const auto stem = requested.stem().wstring();
    const auto extension = requested.extension().wstring();
    const auto name = stem + L" (" + std::to_wstring(index) + L")" + extension;
    return parent / name;
}

} // namespace

Result<std::filesystem::path> CollisionResolver::resolve(
    const std::filesystem::path& requestedDestination,
    CollisionPolicy policy,
    const PathExists& exists) {
    if (!exists(requestedDestination)) {
        return Result<std::filesystem::path>::success(requestedDestination);
    }

    switch (policy) {
    case CollisionPolicy::Skip:
        return Result<std::filesystem::path>::failure({
            "collision.skip",
            "The destination exists and the step must be skipped.",
        });
    case CollisionPolicy::Abort:
        return Result<std::filesystem::path>::failure({
            "collision.abort",
            "The destination exists and the plan must be aborted.",
        });
    case CollisionPolicy::Rename:
        for (int index = 1; index <= maximumRenameAttempts; ++index) {
            auto candidate = renamedCandidate(requestedDestination, index);
            if (!exists(candidate)) {
                return Result<std::filesystem::path>::success(std::move(candidate));
            }
        }
        return Result<std::filesystem::path>::failure({
            "collision.rename_exhausted",
            "No available renamed destination was found.",
        });
    }

    return Result<std::filesystem::path>::failure({
        "collision.unknown_policy",
        "The collision policy is not supported.",
    });
}

} // namespace nookbit
