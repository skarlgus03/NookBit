#include "modules/planner/path_safety_validator.hpp"

#include <algorithm>
#include <cwctype>
#include <string>

namespace nookbit {

namespace {

// 대상 경로가 설정 폴더 안에 있는지 확인하기 위한 정규 경로를 만드는 함수.
std::filesystem::path normalizedAbsolute(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return normalized.lexically_normal();
    }
    return std::filesystem::absolute(path, error).lexically_normal();
}

// Windows 경로 구성 요소를 대소문자 구분 없이 비교하도록 변환하는 함수.
std::wstring normalizedComponent(const std::filesystem::path& component) {
    auto value = component.wstring();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
#endif
    return value;
}

// 정리 폴더가 드라이브 루트인지 확인하는 함수.
bool isDriveRoot(const std::filesystem::path& path) {
    const auto normalized = normalizedAbsolute(path);
    return normalized.has_root_path() && normalized == normalized.root_path();
}

// 선택된 파일과 출발지가 같은 경로인지 폴더 범위를 넓히지 않고 비교하는 함수.
bool sameSelectedPath(const std::filesystem::path& left,
                      const std::filesystem::path& right) {
    if (!left.is_absolute() || !right.is_absolute()) return false;
    const auto normalizedLeft = left.lexically_normal();
    const auto normalizedRight = right.lexically_normal();
    auto leftPart = normalizedLeft.begin();
    auto rightPart = normalizedRight.begin();
    for (; leftPart != normalizedLeft.end() && rightPart != normalizedRight.end();
         ++leftPart, ++rightPart) {
        if (normalizedComponent(*leftPart) != normalizedComponent(*rightPart)) return false;
    }
    return leftPart == normalizedLeft.end() && rightPart == normalizedRight.end();
}

} // namespace

// 대상 경로가 정리 폴더의 하위에 머무는지 확인하는 함수.
bool PathSafetyValidator::isWithin(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) {
    const auto normalizedChild = normalizedAbsolute(child);
    const auto normalizedParent = normalizedAbsolute(parent);

    auto childIterator = normalizedChild.begin();
    auto parentIterator = normalizedParent.begin();

    for (; parentIterator != normalizedParent.end(); ++parentIterator, ++childIterator) {
        if (childIterator == normalizedChild.end()) {
            return false;
        }
        if (normalizedComponent(*childIterator) != normalizedComponent(*parentIterator)) {
            return false;
        }
    }

    return true;
}

// 선택한 파일만 승인된 목적지로 이동 가능한지 검증하는 함수.
Result<void> PathSafetyValidator::validateMove(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::vector<std::filesystem::path>& selectedFiles,
    const std::filesystem::path& homeRoot,
    const std::filesystem::path& appDataRoot,
    const std::vector<std::filesystem::path>& allowedDestinationRoots) {
    if (source.empty() || destination.empty() || homeRoot.empty()) {
        return Result<void>::failure({
            "path.empty",
            "Source, destination, and home root must be specified.",
        });
    }

    if (isDriveRoot(homeRoot)) {
        return Result<void>::failure({
            "path.home_is_drive_root",
            "A drive root cannot be used as the organizer home.",
        });
    }

    const bool sourceAllowed = std::any_of(
        selectedFiles.begin(),
        selectedFiles.end(),
        [&source](const auto& selected) { return sameSelectedPath(source, selected); });

    if (!sourceAllowed) {
        return Result<void>::failure({
            "path.source_not_selected",
            "The source was not explicitly selected for organizing.",
        });
    }

    // 외부 목적지는 드라이브 루트가 아닌 명시적으로 허용한 폴더 안에서만 사용한다.
    const bool externalAllowed = std::any_of(allowedDestinationRoots.begin(), allowedDestinationRoots.end(),
        [&destination](const auto& root) {
            return root.is_absolute() && !isDriveRoot(root) && isWithin(destination, root);
        });
    if (!isWithin(destination, homeRoot) && !externalAllowed) {
        return Result<void>::failure({
            "path.destination_outside_home",
            "The destination is outside the organizer home and user-approved folders.",
        });
    }

    if (!appDataRoot.empty() &&
        (isWithin(source, appDataRoot) || isWithin(destination, appDataRoot))) {
        return Result<void>::failure({
            "path.app_data_protected",
            "Application data paths cannot be organized.",
        });
    }

    if (normalizedAbsolute(source) == normalizedAbsolute(destination)) {
        return Result<void>::failure({
            "path.same_source_and_destination",
            "Source and destination refer to the same path.",
        });
    }

    return Result<void>::success();
}

} // namespace nookbit
