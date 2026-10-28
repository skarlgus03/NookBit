#include "infrastructure/filesystem/filesystem_scanner.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace nookbit {
namespace {

// 경로를 오류 메시지용 UTF-8 문자열로 변환하는 함수.
std::string utf8Path(const std::filesystem::path& path) {
    const auto characters = path.u8string();
    std::string result;
    result.reserve(characters.size());
    for (const char8_t character : characters) result.push_back(static_cast<char>(character));
    return result;
}

// 대소문자를 구분하지 않는 ASCII 패턴 비교에 사용할 글자를 만드는 함수.
char lowerAscii(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

// 별표와 물음표를 지원하는 파일명 패턴을 검사하는 함수.
bool matchesGlob(std::string_view pattern, std::string_view fileName) {
    std::size_t patternIndex = 0;
    std::size_t nameIndex = 0;
    std::size_t lastStar = std::string_view::npos;
    std::size_t nameAfterStar = 0;
    while (nameIndex < fileName.size()) {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == '?' ||
             lowerAscii(pattern[patternIndex]) == lowerAscii(fileName[nameIndex]))) {
            ++patternIndex;
            ++nameIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            lastStar = patternIndex++;
            nameAfterStar = nameIndex;
        } else if (lastStar != std::string_view::npos) {
            patternIndex = lastStar + 1;
            nameIndex = ++nameAfterStar;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') ++patternIndex;
    return patternIndex == pattern.size();
}

// 감시 모드에서 파일명이 제외 패턴 중 하나에 맞는지 확인하는 함수.
bool isExcluded(const std::filesystem::path& path,
                const std::vector<std::string>& patterns) {
    const auto name = utf8Path(path.filename());
    return std::any_of(patterns.begin(), patterns.end(),
        [&name](const std::string& pattern) { return matchesGlob(pattern, name); });
}

// Windows에서 대소문자만 다른 중복 경로를 하나로 취급하기 위한 키를 만드는 함수.
std::wstring pathKey(const std::filesystem::path& path) {
    auto key = path.lexically_normal().wstring();
#ifdef _WIN32
    for (auto& character : key) character = static_cast<wchar_t>(std::towlower(character));
#endif
    return key;
}

// Windows 재분석 지점 또는 다른 플랫폼의 심볼릭 링크인지 확인하는 함수.
bool isReparsePoint(const std::filesystem::path& path, std::error_code& error) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    error.clear();
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_symlink(status);
#endif
}

// 선택 파일이나 감시 폴더로 이어지는 경로가 링크를 지나지 않는지 확인하는 함수.
Result<void> validatePath(const std::filesystem::path& path, bool requireDirectory) {
    if (!path.is_absolute() || path == path.root_path()) {
        return Result<void>::failure({
            "scan.invalid_path", "Selection must be an absolute path below a volume root.",
        });
    }
    auto componentPath = path.root_path();
    for (const auto& component : path.relative_path()) {
        componentPath /= component;
        std::error_code error;
        if (isReparsePoint(componentPath, error)) {
            return Result<void>::failure({
                "scan.reparse_path", "Selected path traverses a reparse point: " + utf8Path(componentPath),
            });
        }
        if (error) {
            return Result<void>::failure({
                "scan.path_unavailable", "Cannot inspect selected path: " + utf8Path(componentPath),
            });
        }
    }
    std::error_code error;
    const bool correctType = requireDirectory
        ? std::filesystem::is_directory(path, error)
        : std::filesystem::is_regular_file(path, error);
    if (error || !correctType) {
        return Result<void>::failure({
            "scan.wrong_type", "Selected path is not an accessible " +
                std::string(requireDirectory ? "directory: " : "file: ") + utf8Path(path),
        });
    }
    return Result<void>::success();
}

// 파일 수정 시각을 UTC 밀리초로 읽는 함수.
Result<std::int64_t> modifiedUtcMs(const std::filesystem::path& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return Result<std::int64_t>::failure({
            "scan.metadata_unavailable", "Cannot read modified time: " + utf8Path(path),
        });
    }
    ULARGE_INTEGER ticks{};
    ticks.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    ticks.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    constexpr std::int64_t windowsEpochMs = 11644473600000LL;
    return Result<std::int64_t>::success(
        static_cast<std::int64_t>(ticks.QuadPart / 10000ULL) - windowsEpochMs);
#else
    std::error_code error;
    const auto fileTime = std::filesystem::last_write_time(path, error);
    if (error) {
        return Result<std::int64_t>::failure({
            "scan.metadata_unavailable", "Cannot read modified time: " + utf8Path(path),
        });
    }
    const auto systemTime = std::chrono::system_clock::now() +
        (fileTime - std::filesystem::file_time_type::clock::now());
    return Result<std::int64_t>::success(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            systemTime.time_since_epoch()).count());
#endif
}

// 파일의 크기·수정 시각·상대 경로를 FileFact로 만드는 함수.
Result<FileFact> readFileFact(const std::filesystem::path& path,
                              const std::filesystem::path& relativePath) {
    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error);
    const auto size = directory ? 0 : std::filesystem::file_size(path, error);
    if (error) {
        return Result<FileFact>::failure({
            "scan.metadata_unavailable", "Cannot read file size: " + utf8Path(path),
        });
    }
    auto modified = modifiedUtcMs(path);
    if (!modified) return Result<FileFact>::failure(modified.error());
    FileFact fact;
    fact.absolutePath = path;
    fact.relativePath = relativePath;
    fact.extension = directory ? std::string{} : utf8Path(path.extension());
    fact.fingerprint.directory = directory;
    fact.fingerprint.size = size;
    fact.fingerprint.modifiedUtcMs = modified.value();
#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Result<FileFact>::failure({
            "scan.identity_unavailable", "Cannot identify selected file: " + utf8Path(path),
        });
    }
    BY_HANDLE_FILE_INFORMATION identity{};
    const BOOL identified = GetFileInformationByHandle(handle, &identity);
    CloseHandle(handle);
    if (!identified) {
        return Result<FileFact>::failure({
            "scan.identity_unavailable", "Cannot identify selected file: " + utf8Path(path),
        });
    }
    fact.fingerprint.volumeId = std::to_string(identity.dwVolumeSerialNumber);
    const std::uint64_t fileNumber =
        (static_cast<std::uint64_t>(identity.nFileIndexHigh) << 32U) |
        static_cast<std::uint64_t>(identity.nFileIndexLow);
    fact.fingerprint.fileId = std::to_string(fileNumber);
#endif
    return Result<FileFact>::success(std::move(fact));
}

// 감시 폴더에서 만난 항목을 검사하고 하위 폴더 진입 가능 여부를 반환하는 함수.
bool collectFolderEntry(const std::filesystem::directory_entry& entry,
                        const std::filesystem::path& root,
                        const ScanRequest& request,
                        ScanSnapshot& snapshot,
                        std::set<std::wstring>& seenFiles) {
    const auto path = entry.path();
    std::error_code error;
    if (isReparsePoint(path, error)) {
        snapshot.warnings.push_back({"scan.reparse_skipped", "Skipped reparse point: " + utf8Path(path)});
        return false;
    }
    if (error) {
        snapshot.warnings.push_back({"scan.entry_unavailable", "Cannot inspect entry: " + utf8Path(path)});
        return false;
    }
    const bool directory = entry.is_directory(error);
    if (error) {
        snapshot.warnings.push_back({"scan.entry_unavailable", "Cannot inspect entry: " + utf8Path(path)});
        return false;
    }
    if (directory) return true;
    if (!entry.is_regular_file(error) || error) {
        if (error) snapshot.warnings.push_back({"scan.entry_unavailable", "Cannot inspect entry: " + utf8Path(path)});
        return false;
    }
    if (isExcluded(path, request.excludePatterns) || !seenFiles.insert(pathKey(path)).second) {
        return false;
    }
    auto fact = readFileFact(path, path.lexically_relative(root));
    if (fact) snapshot.files.push_back(std::move(fact.value()));
    else snapshot.warnings.push_back(fact.error());
    return false;
}

// 감시 폴더를 요청한 깊이만큼 읽는 함수.
Result<void> scanFolder(const std::filesystem::path& root, const ScanRequest& request,
                        ScanSnapshot& snapshot, std::set<std::wstring>& seenFiles) {
    auto checked = validatePath(root, true);
    if (!checked) return checked;
    std::error_code error;
    if (request.recursive) {
        std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::none, error);
        if (error) return Result<void>::failure({"scan.enumeration_failed", "Cannot list folder: " + utf8Path(root)});
        const std::filesystem::recursive_directory_iterator end;
        while (iterator != end) {
            const auto path = iterator->path();
            if (!collectFolderEntry(*iterator, root, request, snapshot, seenFiles)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            if (error) {
                snapshot.warnings.push_back({"scan.enumeration_incomplete", "Cannot continue after: " + utf8Path(path)});
                break;
            }
        }
    } else {
        std::filesystem::directory_iterator iterator(root, std::filesystem::directory_options::none, error);
        if (error) return Result<void>::failure({"scan.enumeration_failed", "Cannot list folder: " + utf8Path(root)});
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            const auto path = iterator->path();
            collectFolderEntry(*iterator, root, request, snapshot, seenFiles);
            iterator.increment(error);
            if (error) {
                snapshot.warnings.push_back({"scan.enumeration_incomplete", "Cannot continue after: " + utf8Path(path)});
                break;
            }
        }
    }
    return Result<void>::success();
}

} // namespace

// 직접 선택한 파일 또는 감시 폴더 중 한 입력 방식만 읽는 함수.
Result<ScanSnapshot> FilesystemScanner::scan(const ScanRequest& request) {
    const bool hasSelectedFiles = !request.selectedFiles.empty();
    const bool hasRoots = !request.roots.empty();
    if (hasSelectedFiles == hasRoots) {
        return Result<ScanSnapshot>::failure({
            "scan.input_mode_invalid", "Provide either selected files or watch folders, but not both.",
        });
    }
    static std::atomic<std::uint64_t> nextId{0};
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ScanSnapshot snapshot;
    snapshot.id = "scan-" + std::to_string(nowMs) + "-" + std::to_string(++nextId);
    std::set<std::wstring> seenFiles;
    if (hasSelectedFiles) {
        for (const auto& selected : request.selectedFiles) {
            const auto path = selected.lexically_normal();
            if (!seenFiles.insert(pathKey(path)).second) continue;
            std::error_code typeError;
            const bool directory = std::filesystem::is_directory(path, typeError);
            auto checked = validatePath(path, directory);
            if (!checked) return Result<ScanSnapshot>::failure(checked.error());
            auto fact = readFileFact(path, path.filename());
            if (!fact) return Result<ScanSnapshot>::failure(fact.error());
            snapshot.files.push_back(std::move(fact.value()));
        }
    } else {
        for (const auto& configuredRoot : request.roots) {
            const auto root = configuredRoot.lexically_normal();
            auto scanned = scanFolder(root, request, snapshot, seenFiles);
            if (!scanned) return Result<ScanSnapshot>::failure(scanned.error());
        }
        std::sort(snapshot.files.begin(), snapshot.files.end(),
            [](const FileFact& left, const FileFact& right) {
                return pathKey(left.absolutePath) < pathKey(right.absolutePath);
            });
    }
    return Result<ScanSnapshot>::success(std::move(snapshot));
}

} // namespace nookbit
