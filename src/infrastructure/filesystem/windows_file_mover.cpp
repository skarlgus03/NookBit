#include "infrastructure/filesystem/windows_file_mover.hpp"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

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

// 같은 객체의 크기·수정 시각을 보호된 핸들에서 검사하고 덮어쓰기 없이 이동하는 함수.
Result<void> WindowsFileMover::moveMatchingNoReplace(const std::filesystem::path& source,
    const std::filesystem::path& destination, const FileFingerprint& expected) {
#ifdef _WIN32
    if (!source.is_absolute() || !destination.is_absolute() || source == destination || !expected.volumeId || !expected.fileId)
        return Result<void>::failure({"move.path_invalid", "Absolute distinct paths and an object identity are required."});
    for (const auto& path : {source, destination}) for (const auto& component : path.relative_path()) {
        const auto name = component.wstring();
        if (name == L"." || name == L".." || name.find(L':') != std::wstring::npos)
            return Result<void>::failure({"move.path_invalid", "Alternate streams and path traversal are not supported."});
    }
    const HANDLE raw = CreateFileW(source.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) return systemFailure("move.source_open_failed", GetLastError());
    const auto close = [](void* handle) { CloseHandle(handle); };
    std::unique_ptr<void, decltype(close)> protectedSource(raw, close);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(raw, &info)) return systemFailure("move.identity_failed", GetLastError());
    const auto id = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    const auto size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const auto ticks = (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    const auto modified = static_cast<int64_t>(ticks / 10000ULL) - 11644473600000LL;
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks != 1 ||
        std::to_string(info.dwVolumeSerialNumber) != *expected.volumeId || std::to_string(id) != *expected.fileId ||
        size != expected.size || modified != expected.modifiedUtcMs)
        return Result<void>::failure({"move.source_changed", "Source identity, size, modified time or supported file type changed."});
    const auto parent = prepareParent(destination);
    if (!parent) return parent;
    // 양쪽 부모를 이동·교체로부터 보호하고 링크 및 다른 볼륨으로의 우회를 거부한다.
    std::vector<std::unique_ptr<void, decltype(close)>> parents;
    for (const auto& path : {source, destination}) {
        auto current = path.root_path();
        std::vector<std::filesystem::path> chain{current};
        for (const auto& component : path.parent_path().relative_path()) { current /= component; chain.push_back(current); }
        for (const auto& directory : chain) {
            const auto handle = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return systemFailure("move.parent_unavailable", GetLastError());
            parents.emplace_back(handle, close);
            BY_HANDLE_FILE_INFORMATION parentInfo{};
            if (!GetFileInformationByHandle(handle, &parentInfo)) return systemFailure("move.parent_unavailable", GetLastError());
            if (!(parentInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (parentInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                std::to_string(parentInfo.dwVolumeSerialNumber) != *expected.volumeId)
                return Result<void>::failure({"move.unsafe_parent", "Protected parent is a link, non-directory or different volume."});
        }
    }
    const auto name = destination.wstring();
    const size_t bytes = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
    if (bytes > MAXDWORD) return Result<void>::failure({"move.path_invalid", "Destination path is too long."});
    std::vector<uint64_t> storage((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), rename->FileName);
    if (!SetFileInformationByHandle(raw, FileRenameInfo, rename, static_cast<DWORD>(bytes)))
        return systemFailure("move.rename_failed", GetLastError());
    return Result<void>::success();
#else
    (void)source; (void)destination; (void)expected;
    return Result<void>::failure({"move.unsupported_platform", "Windows is required."});
#endif
}

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
