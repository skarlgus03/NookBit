#include "infrastructure/filesystem/windows_file_mover.hpp"

#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace nookbit {
namespace {

// 운영체제 오류 번호를 결과에 넣는 함수.
Result<void> systemFailure(const char* code, unsigned long number) {
    return Result<void>::failure({code, "Windows error " + std::to_string(number)});
}

#ifdef _WIN32
// 목적지까지의 모든 부모 폴더를 링크 없이 확인하거나 생성하는 함수.
Result<void> prepareParent(const std::filesystem::path& destination) {
    auto current = destination.root_path();
    for (const auto& component : destination.parent_path().relative_path()) {
        current /= component;
        DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
                return systemFailure("move.parent_unavailable", error);
            }
            if (!CreateDirectoryW(current.c_str(), nullptr)) {
                const DWORD createError = GetLastError();
                if (createError != ERROR_ALREADY_EXISTS) {
                    return systemFailure("move.mkdir_failed", createError);
                }
            }
            attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                return systemFailure("move.parent_unavailable", GetLastError());
            }
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return Result<void>::failure({
                "move.unsafe_parent", "Destination parent is not a regular directory.",
            });
        }
    }
    return Result<void>::success();
}
#endif

} // namespace

// 링크를 거치지 않는 부모 폴더를 준비하고 파일을 덮어쓰기 없이 옮기는 함수.
Result<void> WindowsFileMover::moveNoReplace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!source.is_absolute() || !destination.is_absolute() || source == destination) {
        return Result<void>::failure({"move.path_invalid", "Absolute, distinct paths are required."});
    }
    auto parent = prepareParent(destination);
    if (!parent) return parent;
    const DWORD existing = GetFileAttributesW(destination.c_str());
    if (existing != INVALID_FILE_ATTRIBUTES) {
        return Result<void>::failure({"move.destination_exists", "Destination already exists."});
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return systemFailure("move.destination_unavailable", GetLastError());
    }
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return systemFailure("move.rename_failed", GetLastError());
    }
    return Result<void>::success();
#else
    (void)source;
    (void)destination;
    return Result<void>::failure({"move.unsupported_platform", "File moves require Windows."});
#endif
}

} // namespace nookbit
