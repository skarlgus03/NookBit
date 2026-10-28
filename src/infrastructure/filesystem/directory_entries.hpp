#pragma once
#include <nookbit/domain/result.hpp>
#include <filesystem>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace nookbit {
struct DirectoryEntry {
    std::filesystem::path relative;
    bool directory{};
};

/// 링크를 따라가지 않고 빈 폴더까지 포함한 묶음의 구성 항목을 읽는 함수.
inline Result<std::vector<DirectoryEntry>> directoryEntries(const std::filesystem::path& root) {
    std::vector<DirectoryEntry> result{{{}, true}};
    std::error_code error;
#ifdef _WIN32
    const auto rootAttributes = GetFileAttributesW(root.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || !(rootAttributes & FILE_ATTRIBUTE_DIRECTORY) || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return Result<std::vector<DirectoryEntry>>::failure({"folder.root_changed", "Selected folder is unavailable or is a link."});
#endif
    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::none, error), end;
    if (error) return Result<std::vector<DirectoryEntry>>::failure({"folder.unavailable", "Cannot enumerate selected folder."});
    for (; it != end; it.increment(error)) {
        if (error) break;
        const auto status = it->symlink_status(error);
        if (error) break;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Result<std::vector<DirectoryEntry>>::failure({"folder.link_unsupported", "Folder contains a link, junction or unavailable entry."});
#endif
        const bool directory = std::filesystem::is_directory(status);
        if (!directory && !std::filesystem::is_regular_file(status))
            return Result<std::vector<DirectoryEntry>>::failure({"folder.type_unsupported", "Folder contains an unsupported item."});
        result.push_back({it->path().lexically_relative(root), directory});
    }
    if (error) return Result<std::vector<DirectoryEntry>>::failure({"folder.enumeration_failed", "Cannot read the complete folder; nothing was removed."});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.relative.generic_u8string() < b.relative.generic_u8string(); });
    return Result<std::vector<DirectoryEntry>>::success(std::move(result));
}
}
