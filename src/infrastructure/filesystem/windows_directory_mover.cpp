#include "infrastructure/filesystem/windows_directory_mover.hpp"
#include "infrastructure/filesystem/directory_entries.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "modules/planner/path_safety_validator.hpp"
#include <nookbit/application/operation_progress.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace nookbit {
#ifdef _WIN32
namespace {
/// 획득한 Windows 핸들을 자동 해제하는 함수.
struct CloseHandleDeleter { void operator()(void* handle) const { CloseHandle(handle); } };
using Handle = std::unique_ptr<void, CloseHandleDeleter>;
/// 실패한 Windows 파일 작업의 원인을 전달하는 함수.
Result<void> failure(const char* code, const std::filesystem::path& staging) {
    const auto number = GetLastError();
    const auto path = staging.generic_u8string();
    return Result<void>::failure({code, "Windows error " + std::to_string(number) +
        "; preserved copy: " + std::string(reinterpret_cast<const char*>(path.data()), path.size())});
}
/// 복사 전후에 경로가 다른 폴더로 바뀌지 않게 모든 조상 폴더를 보호하는 함수.
Result<void> protectParents(const std::filesystem::path& path, std::vector<Handle>& handles) {
    auto current = path.root_path();
    for (const auto& part : path.parent_path().relative_path()) {
        current /= part;
        Handle handle(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) { handle.release(); return failure("folder.parent_unavailable", {}); }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle.get(), &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Result<void>::failure({"folder.unsafe_parent", "Folder path contains a junction or unavailable parent."});
        handles.push_back(std::move(handle));
    }
    return Result<void>::success();
}
/// 원본 스트림은 삭제를 허용한 채 쓰기를 막고, 복사본 스트림은 변경·삭제를 막는 함수.
Result<void> protectStreams(const std::filesystem::path& path, bool source, std::vector<Handle>& handles) {
    WIN32_FIND_STREAM_DATA stream{};
    const auto search = FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &stream, 0);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_HANDLE_EOF ? Result<void>::success() : failure("folder.stream_unavailable", {});
    std::vector<std::wstring> names;
    do { if (std::wstring_view(stream.cStreamName) != L"::$DATA") names.emplace_back(stream.cStreamName); }
    while (FindNextStreamW(search, &stream));
    const auto last = GetLastError(); FindClose(search);
    if (last != ERROR_HANDLE_EOF) return failure("folder.stream_unavailable", {});
    for (const auto& name : names) {
        Handle handle(CreateFileW((path.wstring() + name).c_str(), GENERIC_READ,
            FILE_SHARE_READ | (source ? FILE_SHARE_DELETE : 0), nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) { handle.release(); return failure("folder.stream_in_use", {}); }
        handles.push_back(std::move(handle));
    }
    return Result<void>::success();
}
/// 원본 폴더의 생성·수정 시각을 복사된 폴더에 보존하는 함수.
Result<void> copyDirectoryTimes(const std::filesystem::path& source, const std::filesystem::path& target) {
    Handle a(CreateFileW(source.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Handle b(CreateFileW(target.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (a.get() == INVALID_HANDLE_VALUE || b.get() == INVALID_HANDLE_VALUE) {
        if (a.get() == INVALID_HANDLE_VALUE) a.release(); if (b.get() == INVALID_HANDLE_VALUE) b.release();
        return failure("folder.metadata_failed", target);
    }
    FILETIME creation{}, access{}, write{};
    if (!GetFileTime(a.get(), &creation, &access, &write) || !SetFileTime(b.get(), &creation, &access, &write))
        return failure("folder.metadata_failed", target);
    return Result<void>::success();
}
}
#endif

/// 복사 성공·내용 일치·최종 경로 확정 이후에만 보호한 원본 항목을 제거하는 함수.
Result<void> WindowsDirectoryMover::move(const std::filesystem::path& source,
    const std::filesystem::path& destination, const FileFingerprint& expected,
    IScanner& scanner, const IPlanHasher& hasher) {
#ifdef _WIN32
    if (!expected.directory || !expected.sha256 || !source.is_absolute() || !destination.is_absolute())
        return Result<void>::failure({"folder.approval_required", "An approved directory content digest is required."});
    if (source == source.root_path() || PathSafetyValidator::isWithin(destination, source))
        return Result<void>::failure({"folder.invalid_destination", "A folder cannot be moved into itself or its descendants."});
    auto identity = expected; identity.sha256.reset();
    const auto current = scanFingerprint(scanner, source);
    if (!current || !sameFingerprint(identity, current.value()))
        return Result<void>::failure({"folder.source_changed", "Selected folder identity changed before copying."});
    std::vector<Handle> parentLocks;
    const auto sourceParents = protectParents(source, parentLocks);
    if (!sourceParents) return sourceParents;
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return Result<void>::failure({"folder.mkdir_failed", "Cannot create destination parent."});
    const auto destinationParents = protectParents(destination, parentLocks);
    if (!destinationParents) return destinationParents;
    if (std::filesystem::exists(destination, error) || error)
        return Result<void>::failure({"folder.destination_exists", "Destination name is already in use; folder was not moved."});
    const auto entries = directoryEntries(source);
    if (!entries) return Result<void>::failure(entries.error());
    static std::atomic<unsigned long long> next{0};
    const auto staging = destination.parent_path() / (L".nookbit-folder-" +
        std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L"-" + std::to_wstring(++next) + L".tmp");
    if (!CreateDirectoryW(staging.c_str(), nullptr)) return failure("folder.staging_failed", staging);
    // 실패 시 임시 복사본을 보존하고, 원본은 아래의 최종 검증까지 지우지 않는다.
    OperationProgressScope::phase(OperationPhase::Copying);
    for (const auto& entry : entries.value()) {
        if (entry.relative.empty()) continue;
        const auto from = source / entry.relative, to = staging / entry.relative;
        if (entry.directory) {
            if (!CreateDirectoryW(to.c_str(), nullptr)) return failure("folder.copy_failed", staging);
        } else {
            if (!CopyFileExW(from.c_str(), to.c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS))
                return failure("folder.copy_failed", staging);
            // 원본 제거 전에 복사한 바이트를 디스크에 확정한다.
            Handle flush(CreateFileW(to.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (flush.get() == INVALID_HANDLE_VALUE) { flush.release(); return failure("folder.flush_failed", staging); }
            if (!FlushFileBuffers(flush.get())) return failure("folder.flush_failed", staging);
        }
    }
    for (auto it = entries.value().rbegin(); it != entries.value().rend(); ++it) if (it->directory) {
        const auto metadata = copyDirectoryTimes(source / it->relative, staging / it->relative);
        if (!metadata) return metadata;
    }
    std::vector<Handle> sourceLocks, copyLocks;
    std::vector<std::vector<Handle>> sourceStreams, copyStreams;
    for (const auto& entry : entries.value()) {
        const auto from = source / entry.relative, to = staging / entry.relative;
        const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS;
        Handle a(CreateFileW(from.c_str(), DELETE | FILE_READ_ATTRIBUTES | (entry.directory ? 0 : GENERIC_READ),
            FILE_SHARE_READ | (entry.directory ? FILE_SHARE_WRITE : 0), nullptr, OPEN_EXISTING, flags, nullptr));
        Handle b(CreateFileW(to.c_str(), FILE_READ_ATTRIBUTES | (entry.directory ? 0 : GENERIC_READ),
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, flags, nullptr));
        if (a.get() == INVALID_HANDLE_VALUE || b.get() == INVALID_HANDLE_VALUE) {
            if (a.get() == INVALID_HANDLE_VALUE) a.release(); if (b.get() == INVALID_HANDLE_VALUE) b.release();
            return failure("folder.in_use", staging);
        }
        BY_HANDLE_FILE_INFORMATION aInfo{}, bInfo{};
        if (!GetFileInformationByHandle(a.get(), &aInfo) || !GetFileInformationByHandle(b.get(), &bInfo) ||
            (aInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || (bInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Result<void>::failure({"folder.changed", "A folder entry changed during copying; original retained."});
        sourceStreams.emplace_back(); copyStreams.emplace_back();
        if (!entry.directory) {
            const auto protectedSource = protectStreams(from, true, sourceStreams.back());
            if (!protectedSource) return protectedSource;
            const auto protectedCopy = protectStreams(to, false, copyStreams.back());
            if (!protectedCopy) return protectedCopy;
        }
        sourceLocks.push_back(std::move(a)); copyLocks.push_back(std::move(b));
    }
    OperationProgressScope::phase(OperationPhase::VerifyingCopy);
    const auto sourceDigest = hasher.sha256File(source), copyDigest = hasher.sha256File(staging);
    const auto sourceIdentity = scanFingerprint(scanner, source);
    if (!sourceDigest) return Result<void>::failure({"folder.source_digest_failed", sourceDigest.error().code + ": " + sourceDigest.error().message});
    if (!copyDigest) return Result<void>::failure({"folder.copy_digest_failed", copyDigest.error().code + ": " + copyDigest.error().message});
    if (!sourceIdentity) return Result<void>::failure({"folder.source_identity_failed", sourceIdentity.error().message});
    if (!sourceDigest || !copyDigest || sourceDigest.value() != *expected.sha256 || copyDigest.value() != *expected.sha256 ||
        !sourceIdentity || !sameFingerprint(identity, sourceIdentity.value()))
        return Result<void>::failure({"folder.content_changed", "Folder copy did not match approval; original and staging were preserved."});
    OperationProgressScope::phase(OperationPhase::CommittingDestination);
    const auto stagingIdentity = scanFingerprint(scanner, staging);
    if (!stagingIdentity) return Result<void>::failure(stagingIdentity.error());
    // Windows에서는 열린 자식 핸들이 폴더 이름 변경을 막는다. 복사본만 잠깐 풀고,
    // 확정한 경로에서 다시 보호·검증할 때까지 원본 잠금과 원본 데이터는 유지한다.
    copyLocks.clear(); copyStreams.clear();
    const auto committed = WindowsFileMover::moveMatchingNoReplace(staging, destination, stagingIdentity.value());
    if (!committed) return Result<void>::failure({"folder.commit_failed", committed.error().message});
    for (const auto& entry : entries.value()) {
        const auto path = destination / entry.relative;
        Handle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | (entry.directory ? 0 : GENERIC_READ),
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) { handle.release(); return failure("folder.committed_copy_in_use", destination); }
        copyLocks.push_back(std::move(handle)); copyStreams.emplace_back();
        if (!entry.directory) {
            const auto locked = protectStreams(path, false, copyStreams.back());
            if (!locked) return locked;
        }
    }
    const auto finalDigest = hasher.sha256File(destination);
    if (!finalDigest || finalDigest.value() != *expected.sha256)
        return Result<void>::failure({"folder.committed_copy_changed", "Committed folder changed; original was preserved."});
    OperationProgressScope::phase(OperationPhase::RemovingSource);
    // 목록에 없던 새 항목은 삭제하지 않는다. 비어 있지 않은 폴더 제거는 Windows가 거부한다.
    for (std::size_t index = sourceLocks.size(); index-- > 0;) {
        FILE_DISPOSITION_INFO remove{TRUE};
        if (!SetFileInformationByHandle(sourceLocks[index].get(), FileDispositionInfo, &remove, sizeof(remove)))
            return failure("folder.cleanup_needs_review", destination);
        sourceStreams[index].clear();
        sourceLocks[index].reset();
    }
    return Result<void>::success();
#else
    (void)source; (void)destination; (void)expected; (void)scanner; (void)hasher;
    return Result<void>::failure({"folder.unsupported_platform", "Folder moves require Windows."});
#endif
}
}
