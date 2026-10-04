#include "windows_staged_copy.hpp"
#include "windows_transfer_file_inspector.hpp"
#include "windows_transfer_source_removal.hpp"
#include "windows_transfer_recovery.hpp"
#include "file_identity.hpp"
#include <nookbit/domain/transfer.hpp>

namespace nookbit {
// 기존 승인 형식의 복사 세션을 여는 함수.
Result<std::unique_ptr<IStagedCopySession>> WindowsStagedCopy::open(
    const std::filesystem::path& source, const FileFingerprint& expected, const std::filesystem::path& staging) {
    return openImpl(source, expected, staging, nullptr);
}
// v2 계약을 보호된 파일·부모 핸들에서 확인하는 세션을 여는 함수.
Result<std::unique_ptr<IStagedCopySession>> WindowsStagedCopy::openApproved(
    const std::filesystem::path& source, const FileFingerprint& expected, const std::filesystem::path& staging, const MoveContract& movement) {
    return openImpl(source, expected, staging, &movement);
}
// 기존 승인 형식의 제거 세션을 여는 함수.
Result<std::unique_ptr<ITransferRemovalSession>> WindowsTransferSourceRemoval::open(const TransferAttempt& attempt) {
    return openImpl(attempt, nullptr);
}
// v2 계약을 보호된 두 파일 핸들에서 확인하는 제거 세션을 여는 함수.
Result<std::unique_ptr<ITransferRemovalSession>> WindowsTransferSourceRemoval::openApproved(const TransferAttempt& attempt, const MoveContract& movement) {
    return openImpl(attempt, &movement);
}
}

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace nookbit {
namespace {

// Windows 오류 번호를 포함한 작업 오류를 만드는 함수.
Error failure(const char* code) {
    return {code, std::string(code) + " (Windows " + std::to_string(GetLastError()) + ")"};
}

struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    // 열린 핸들의 소유권을 받는 함수.
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    // 핸들 소유권을 이전하는 함수.
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle(const Handle&) = delete;
    // 파일을 삭제하지 않고 핸들만 닫는 함수.
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

// Windows 장치 예약 이름을 파일·폴더 이름으로 사용하지 못하게 하는 함수.
bool reservedComponent(const std::wstring& text) {
    auto name = text.substr(0, text.find(L'.'));
    while (!name.empty() && name.back() == L' ') name.pop_back();
    for (const auto device : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
        if (_wcsicmp(name.c_str(), device) == 0) return true;
    if (name.size() == 4 && (_wcsnicmp(name.c_str(), L"COM", 3) == 0 || _wcsnicmp(name.c_str(), L"LPT", 3) == 0))
        return (name.back() >= L'1' && name.back() <= L'9') || name.back() == L'\u00b9' ||
            name.back() == L'\u00b2' || name.back() == L'\u00b3';
    return false;
}

// 우회 경로·대체 스트림·장치 이름·네트워크 경로를 받지 않는 함수.
bool validPath(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.root_name().wstring().size() != 2 ||
        path.native().find(wchar_t{}) != std::wstring::npos) return false;
    for (const auto& part : path.relative_path()) {
        const auto text = part.wstring();
        if (text == L".." || text == L"." || reservedComponent(text) || text.find(L':') != std::wstring::npos ||
            (!text.empty() && (text.back() == L' ' || text.back() == L'.'))) return false;
    }
    return true;
}

// 파일 객체의 크기·시간·ID를 핸들에서 읽는 함수.
Result<FileFingerprint> fingerprint(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
        return Result<FileFingerprint>::failure(failure("transfer.stat"));
    ULARGE_INTEGER time{};
    time.HighPart = info.ftLastWriteTime.dwHighDateTime;
    time.LowPart = info.ftLastWriteTime.dwLowDateTime;
    FileFingerprint fp;
    fp.size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    fp.modifiedUtcMs = static_cast<int64_t>(time.QuadPart / 10000) - 11644473600000LL;
    fp.volumeId = std::to_string(info.dwVolumeSerialNumber);
    fp.fileId = std::to_string((static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
    return Result<FileFingerprint>::success(std::move(fp));
}

// 부모 디렉터리의 이름 변경을 막고 재분석 지점을 거부하는 함수.
Result<void> lockParents(const std::filesystem::path& path, std::vector<Handle>& handles) {
    auto current = path.root_path();
    std::vector<std::filesystem::path> paths{current};
    for (const auto& part : path.parent_path().relative_path()) {
        current /= part;
        paths.push_back(current);
    }
    for (const auto& parent : paths) {
        Handle handle(CreateFileW(parent.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info)) {
            auto error = failure("transfer.parent_unavailable");
            const auto encoded = parent.u8string();
            error.message += " at ";
            error.message.append(encoded.begin(), encoded.end());
            return Result<void>::failure(std::move(error));
        }
        if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Result<void>::failure({"transfer.parent_unsupported", "Reparse parents are unsupported"});
        handles.push_back(std::move(handle));
    }
    return Result<void>::success();
}

// 열린 경로의 NTFS·로컬 볼륨 여부를 조회하는 함수.
Result<TransferVolume> volume(HANDLE handle) {
    DWORD serial{};
    wchar_t fs[MAX_PATH]{}, name[MAX_PATH]{};
    std::array<wchar_t, 32768> finalPath{}, mount{};
    const DWORD count = GetFinalPathNameByHandleW(handle, finalPath.data(),
        static_cast<DWORD>(finalPath.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count >= finalPath.size() ||
        !GetVolumePathNameW(finalPath.data(), mount.data(), static_cast<DWORD>(mount.size())) ||
        !GetVolumeInformationByHandleW(handle, nullptr, 0, &serial, nullptr, nullptr, fs, MAX_PATH) ||
        !GetVolumeNameForVolumeMountPointW(mount.data(), name, MAX_PATH))
        return Result<TransferVolume>::failure(failure("transfer.volume"));
    TransferVolume result{std::to_string(serial), name, fs, false};
    result.localNtfs = _wcsicmp(fs, L"NTFS") == 0 && GetDriveTypeW(mount.data()) == DRIVE_FIXED;
    return Result<TransferVolume>::success(std::move(result));
}

// 드라이브 문자 대신 보호된 핸들의 실제 GUID·일련번호·파일 시스템을 비교하는 함수.
bool approvedVolume(const TransferVolume& actual, const PlanVolume& approved) {
    const std::wstring guid(approved.guidPath.begin(), approved.guidPath.end());
    return actual.localNtfs && actual.id == approved.serial && approved.filesystem == "NTFS" &&
        _wcsicmp(actual.filesystem.c_str(), L"NTFS") == 0 && _wcsicmp(actual.name.c_str(), guid.c_str()) == 0;
}

// 승인 볼륨의 부모를 한 단계씩 보호하며 없는 폴더만 생성하고 링크 우회를 거부하는 함수.
Result<void> prepareApprovedParents(const std::filesystem::path& path, const PlanVolume& approved,
    std::vector<Handle>& handles) {
    auto current = path.root_path();
    std::vector<std::filesystem::path> paths{current};
    for (const auto& part : path.parent_path().relative_path()) {
        current /= part;
        paths.push_back(current);
    }
    for (size_t index = 0; index < paths.size(); ++index) {
        const auto& parent = paths[index];
        HANDLE raw = CreateFileW(parent.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (raw == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND && index != 0) {
            // 바로 위 부모는 이미 잠겨 있고 승인 볼륨임을 확인한 상태다.
            if (!CreateDirectoryW(parent.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                return Result<void>::failure(failure("transfer.parent_create"));
            raw = CreateFileW(parent.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        }
        Handle handle(raw);
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info))
            return Result<void>::failure(failure("transfer.parent_unavailable"));
        if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Result<void>::failure({"transfer.parent_unsupported", "Only ordinary directory parents are supported"});
        const auto actual = volume(handle.value);
        if (!actual || !approvedVolume(actual.value(), approved))
            return Result<void>::failure({"transfer.volume_changed", "Protected destination parent differs from approved volume"});
        handles.push_back(std::move(handle));
    }
    return Result<void>::success();
}

// 경로를 다시 열지 않고 보유한 핸들의 전체 SHA-256을 계산하는 함수.
Result<std::string> digest(HANDLE file, const std::function<bool()>& cancelled) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
        return Result<std::string>::failure(failure("transfer.seek"));
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        // 해시 핸들을 역순으로 해제하는 함수.
        ~Hash() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } state;
    if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(state.algorithm, &state.hash, nullptr, 0, nullptr, 0, 0) < 0)
        return Result<std::string>::failure({"transfer.hash", "Cannot initialize SHA-256"});
    std::vector<unsigned char> buffer(1024 * 1024);
    DWORD read{};
    for (;;) {
        if (cancelled && cancelled())
            return Result<std::string>::failure({"transfer.cancelled", "Copy cancelled; source retained"});
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            return Result<std::string>::failure(failure("transfer.read"));
        if (!read) break;
        if (BCryptHashData(state.hash, buffer.data(), read, 0) < 0)
            return Result<std::string>::failure({"transfer.hash", "Cannot hash file contents"});
    }
    std::array<unsigned char, 32> hash{};
    if (BCryptFinishHash(state.hash, hash.data(), static_cast<ULONG>(hash.size()), 0) < 0)
        return Result<std::string>::failure({"transfer.hash", "Cannot finish SHA-256"});
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : hash) { text += hex[byte >> 4]; text += hex[byte & 15]; }
    return Result<std::string>::success(std::move(text));
}

// 이름 있는 스트림이나 지원하지 않는 파일 형태가 생기지 않았는지 확인하는 함수.
Result<void> ordinarySource(HANDLE handle, const std::filesystem::path& path) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
        return Result<void>::failure(failure("transfer.stat"));
    constexpr DWORD unsupported = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
        FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_SPARSE_FILE |
        FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
    if ((info.dwFileAttributes & unsupported) || info.nNumberOfLinks != 1)
        return Result<void>::failure({"transfer.source_unsupported", "Only ordinary single-link files supported"});
    WIN32_FIND_STREAM_DATA stream{};
    HANDLE streams = FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &stream, 0);
    if (streams == INVALID_HANDLE_VALUE) return Result<void>::failure(failure("transfer.streams"));
    bool ordinary = true;
    do { if (std::wstring(stream.cStreamName) != L"::$DATA") ordinary = false; }
    while (FindNextStreamW(streams, &stream));
    const auto streamError = GetLastError();
    FindClose(streams);
    if (!ordinary || streamError != ERROR_HANDLE_EOF)
        return Result<void>::failure({"transfer.streams", "Named streams or unreadable stream list unsupported"});
    return Result<void>::success();
}

class Session final : public IStagedCopySession {
public:
    std::vector<Handle> parents;
    Handle source, staging;
    FileFingerprint expected, created;
    FILE_BASIC_INFO metadata{};
    std::filesystem::path sourcePath;
    std::filesystem::path stagingPath;
    std::optional<FileFingerprint> verifiedCopy;
    bool attempted{false};
    bool commitAttempted{false};

    // 열린 파일의 소유권과 복사 전 증거를 받는 함수.
    Session(std::vector<Handle> locks, Handle input, Handle output, FileFingerprint fp,
            FileFingerprint initial, FILE_BASIC_INFO basic, std::filesystem::path path,
            std::filesystem::path temporary)
        : parents(std::move(locks)), source(std::move(input)), staging(std::move(output)),
          expected(std::move(fp)), created(std::move(initial)), metadata(basic), sourcePath(std::move(path)),
          stagingPath(std::move(temporary)) {}

    // 빈 임시 파일의 객체 지문을 반환하는 함수.
    const FileFingerprint& createdFingerprint() const override { return created; }

    // 원본을 유지하며 복사·flush·재읽기·메타데이터 검증을 수행하는 함수.
    Result<FileFingerprint> copyAndVerify(const std::function<bool()>& cancelled) override {
        if (std::exchange(attempted, true))
            return Result<FileFingerprint>::failure({"transfer.reused", "Session already attempted"});
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(source.value, zero, nullptr, FILE_BEGIN))
            return Result<FileFingerprint>::failure(failure("transfer.seek"));
        std::vector<unsigned char> buffer(1024 * 1024);
        uintmax_t total{};
        for (;;) {
            if (cancelled && cancelled())
                return Result<FileFingerprint>::failure({"transfer.cancelled", "Copy cancelled; source retained"});
            DWORD read{};
            if (!ReadFile(source.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
                return Result<FileFingerprint>::failure(failure("transfer.read"));
            if (!read) break;
            total += read;
            if (total > expected.size)
                return Result<FileFingerprint>::failure({"transfer.source_changed", "Source size changed"});
            DWORD offset{};
            while (offset < read) {
                DWORD written{};
                if (!WriteFile(staging.value, buffer.data() + offset, read - offset, &written, nullptr) || !written)
                    return Result<FileFingerprint>::failure(failure("transfer.write"));
                offset += written;
            }
        }
        if (!FlushFileBuffers(staging.value))
            return Result<FileFingerprint>::failure(failure("transfer.flush"));
        const auto copiedHash = digest(staging.value, cancelled);
        if (!copiedHash) return Result<FileFingerprint>::failure(copiedHash.error());
        const auto sourceHash = digest(source.value, cancelled);
        if (!sourceHash) return Result<FileFingerprint>::failure(sourceHash.error());
        auto current = fingerprint(source.value);
        if (!current) return current;
        current.value().sha256 = sourceHash.value();
        if (!sameFingerprint(expected, current.value()) || total != expected.size ||
            copiedHash.value() != expected.sha256)
            return Result<FileFingerprint>::failure({"transfer.source_changed", "Copy content or source changed"});
        const auto supported = ordinarySource(source.value, sourcePath);
        if (!supported) return Result<FileFingerprint>::failure(supported.error());
        FILE_BASIC_INFO sourceBasic{};
        if (!GetFileInformationByHandleEx(source.value, FileBasicInfo, &sourceBasic, sizeof(sourceBasic)) ||
            sourceBasic.CreationTime.QuadPart != metadata.CreationTime.QuadPart ||
            sourceBasic.LastWriteTime.QuadPart != metadata.LastWriteTime.QuadPart ||
            sourceBasic.FileAttributes != metadata.FileAttributes)
            return Result<FileFingerprint>::failure({"transfer.source_changed", "Source metadata changed"});
        FILE_BASIC_INFO preserve = metadata;
        preserve.LastAccessTime.QuadPart = 0;
        preserve.ChangeTime.QuadPart = 0;
        preserve.FileAttributes &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
            FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        if (!preserve.FileAttributes) preserve.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileInformationByHandle(staging.value, FileBasicInfo, &preserve, sizeof(preserve)) ||
            !FlushFileBuffers(staging.value))
            return Result<FileFingerprint>::failure(failure("transfer.metadata"));
        FILE_BASIC_INFO actual{};
        if (!GetFileInformationByHandleEx(staging.value, FileBasicInfo, &actual, sizeof(actual)) ||
            actual.CreationTime.QuadPart != metadata.CreationTime.QuadPart ||
            actual.LastWriteTime.QuadPart != metadata.LastWriteTime.QuadPart ||
            actual.FileAttributes != preserve.FileAttributes)
            return Result<FileFingerprint>::failure({"transfer.metadata", "Copied metadata differs"});
        auto verified = fingerprint(staging.value);
        if (!verified) return verified;
        verified.value().sha256 = copiedHash.value();
        if (!TransferValidation::sameObject(created, verified.value()) ||
            !TransferValidation::sameContent(expected, verified.value()))
            return Result<FileFingerprint>::failure({"transfer.verification", "Staging verification failed"});
        verifiedCopy = verified.value();
        return verified;
    }

    // 보유한 임시 파일 핸들로 이름을 바꾸고 최종 파일의 내용·객체를 재검증하는 함수.
    Result<FileFingerprint> commitNoReplace(const std::filesystem::path& destination) override {
        if (!verifiedCopy || std::exchange(commitAttempted, true))
            return Result<FileFingerprint>::failure({"transfer.commit_state", "A verified unused session is required"});
        const auto parent = stagingPath.parent_path().lexically_normal().wstring();
        const auto targetParent = destination.parent_path().lexically_normal().wstring();
        const auto temporary = stagingPath.lexically_normal().wstring();
        const auto target = destination.lexically_normal().wstring();
        if (!validPath(destination) || destination.filename().empty() ||
            CompareStringOrdinal(parent.c_str(), -1, targetParent.c_str(), -1, TRUE) != CSTR_EQUAL ||
            CompareStringOrdinal(temporary.c_str(), -1, target.c_str(), -1, TRUE) == CSTR_EQUAL)
            return Result<FileFingerprint>::failure({"transfer.commit_path", "Commit needs a distinct name in the same parent"});
        const auto sourceCheck = ordinarySource(source.value, sourcePath);
        const auto stagingCheck = ordinarySource(staging.value, stagingPath);
        if (!sourceCheck || !stagingCheck)
            return Result<FileFingerprint>::failure(!sourceCheck ? sourceCheck.error() : stagingCheck.error());
        const auto inputHash = digest(source.value, {});
        const auto copyHash = digest(staging.value, {});
        auto input = fingerprint(source.value);
        auto output = fingerprint(staging.value);
        if (!inputHash || !copyHash || !input || !output)
            return Result<FileFingerprint>::failure({"transfer.commit_check", "Cannot revalidate before commit"});
        input.value().sha256 = inputHash.value();
        output.value().sha256 = copyHash.value();
        if (!sameFingerprint(expected, input.value()) || !sameFingerprint(*verifiedCopy, output.value()))
            return Result<FileFingerprint>::failure({"transfer.commit_changed", "Source or staging changed before commit"});
        FILE_BASIC_INFO inputBasic{}, outputBasic{};
        const DWORD preservedAttributes = metadata.FileAttributes & (FILE_ATTRIBUTE_READONLY |
            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        const DWORD expectedAttributes = preservedAttributes ? preservedAttributes : FILE_ATTRIBUTE_NORMAL;
        if (!GetFileInformationByHandleEx(source.value, FileBasicInfo, &inputBasic, sizeof(inputBasic)) ||
            !GetFileInformationByHandleEx(staging.value, FileBasicInfo, &outputBasic, sizeof(outputBasic)) ||
            inputBasic.CreationTime.QuadPart != metadata.CreationTime.QuadPart ||
            inputBasic.LastWriteTime.QuadPart != metadata.LastWriteTime.QuadPart ||
            inputBasic.FileAttributes != metadata.FileAttributes ||
            outputBasic.CreationTime.QuadPart != metadata.CreationTime.QuadPart ||
            outputBasic.LastWriteTime.QuadPart != metadata.LastWriteTime.QuadPart ||
            outputBasic.FileAttributes != expectedAttributes)
            return Result<FileFingerprint>::failure({"transfer.commit_changed", "Source or staging metadata changed before commit"});
        const auto name = destination.filename().wstring();
        const size_t bytes = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
        if (bytes > MAXDWORD)
            return Result<FileFingerprint>::failure({"transfer.commit_path", "Destination path too long"});
        std::vector<uint64_t> storage((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        rename->ReplaceIfExists = FALSE;
        // 단순 이름과 NULL 루트는 같은 디렉터리 내부 변경만 허용한다.
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::copy(name.begin(), name.end(), rename->FileName);
        using SetInformation = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
        using StatusToError = ULONG (WINAPI*)(NTSTATUS);
        const auto native = GetModuleHandleW(L"ntdll.dll");
        const auto setInformation = reinterpret_cast<SetInformation>(GetProcAddress(native, "NtSetInformationFile"));
        const auto statusToError = reinterpret_cast<StatusToError>(GetProcAddress(native, "RtlNtStatusToDosError"));
        if (!setInformation || !statusToError)
            return Result<FileFingerprint>::failure({"transfer.commit_unsupported", "Native same-directory rename unavailable"});
        IO_STATUS_BLOCK statusBlock{};
        constexpr ULONG renameInformation = 10; // FileRenameInformation
        const auto status = setInformation(staging.value, &statusBlock, rename, static_cast<ULONG>(bytes), renameInformation);
        if (status < 0) {
            SetLastError(statusToError(status));
            return Result<FileFingerprint>::failure(failure("transfer.commit_rename"));
        }
        // NTFS 이름 변경이 보관 속성을 추가할 수 있어 승인 당시 보존 정책을 다시 적용한다.
        FILE_BASIC_INFO preserve = metadata;
        preserve.LastAccessTime.QuadPart = 0;
        preserve.ChangeTime.QuadPart = 0;
        preserve.FileAttributes = expectedAttributes;
        if (!SetFileInformationByHandle(staging.value, FileBasicInfo, &preserve, sizeof(preserve)))
            return Result<FileFingerprint>::failure(failure("transfer.commit_metadata"));
        // 이름 변경 뒤 오류는 확정 전 실패와 구별해 상위 계층에서 복구 필요로 남긴다.
        if (!FlushFileBuffers(staging.value))
            return Result<FileFingerprint>::failure(failure("transfer.commit_flush"));
        const auto finalCheck = ordinarySource(staging.value, destination);
        if (!finalCheck) return Result<FileFingerprint>::failure(finalCheck.error());
        const auto finalHash = digest(staging.value, {});
        auto final = fingerprint(staging.value);
        FILE_BASIC_INFO finalBasic{};
        if (!finalHash || !final ||
            !GetFileInformationByHandleEx(staging.value, FileBasicInfo, &finalBasic, sizeof(finalBasic)))
            return Result<FileFingerprint>::failure({"transfer.commit_verify", "Cannot verify renamed file"});
        final.value().sha256 = finalHash.value();
        if (!sameFingerprint(*verifiedCopy, final.value()) ||
            finalBasic.CreationTime.QuadPart != metadata.CreationTime.QuadPart ||
            finalBasic.LastWriteTime.QuadPart != metadata.LastWriteTime.QuadPart ||
            finalBasic.FileAttributes != expectedAttributes)
            return Result<FileFingerprint>::failure({"transfer.commit_verify", "Renamed file differs from verified copy"});
        return final;
    }
};
// 최종 파일을 보호한 상태에서 승인 원본 핸들만 제거하는 세션.
class RemovalSession final : public ITransferRemovalSession {
    std::vector<Handle> parents_;
    Handle source_, destination_;
    TransferAttempt attempt_;
    bool attempted_{false};
public:
    // 보호 핸들과 확정된 전송 증거를 소유하는 함수.
    RemovalSession(std::vector<Handle> parents, Handle source, Handle destination, TransferAttempt attempt)
        : parents_(std::move(parents)), source_(std::move(source)), destination_(std::move(destination)), attempt_(std::move(attempt)) {}
    // 두 파일의 지원 범위·객체 ID·내용·수정 시각을 보유 핸들로 재검증하는 함수.
    Result<void> verify() override {
        if (source_.value == INVALID_HANDLE_VALUE) return Result<void>::failure({"transfer.removal_state", "Source handle closed"});
        for (const auto& pair : {std::pair{source_.value, attempt_.source}, std::pair{destination_.value, attempt_.destination}}) {
            const auto supported = ordinarySource(pair.first, pair.second);
            if (!supported) return supported;
            const auto hash = digest(pair.first, {});
            const auto actual = fingerprint(pair.first);
            if (!hash || !actual) return Result<void>::failure(!hash ? hash.error() : actual.error());
            auto hashed = actual.value();
            hashed.sha256 = hash.value();
            const auto& expected = pair.first == source_.value ? attempt_.sourceFingerprint : *attempt_.destinationFingerprint;
            if (!sameFingerprint(expected, hashed))
                return Result<void>::failure({"transfer.removal_changed", "Source or final copy changed; neither file removed"});
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(source_.value, &info)) return Result<void>::failure(failure("transfer.removal_stat"));
        if (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
            return Result<void>::failure({"transfer.removal_readonly", "Read-only source is retained; attributes are not forced"});
        FILE_BASIC_INFO sourceBasic{}, finalBasic{};
        const DWORD flags = info.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        if (!GetFileInformationByHandleEx(source_.value, FileBasicInfo, &sourceBasic, sizeof(sourceBasic)) ||
            !GetFileInformationByHandleEx(destination_.value, FileBasicInfo, &finalBasic, sizeof(finalBasic)) ||
            sourceBasic.CreationTime.QuadPart != finalBasic.CreationTime.QuadPart ||
            sourceBasic.LastWriteTime.QuadPart != finalBasic.LastWriteTime.QuadPart ||
            finalBasic.FileAttributes != (flags ? flags : FILE_ATTRIBUTE_NORMAL))
            return Result<void>::failure({"transfer.removal_metadata", "Source and final metadata differ; source retained"});
        if (GetFileAttributesW(attempt_.staging.c_str()) != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
            return Result<void>::failure({"transfer.removal_staging", "Staging path is not provably absent"});
        return Result<void>::success();
    }
    // 제거 직전 재검증하고 핸들 기반 제거·닫기·실제 부재를 한 번 확인하는 함수.
    Result<void> removeVerifiedSource() override {
        if (std::exchange(attempted_, true)) return Result<void>::failure({"transfer.removal_reused", "Removal already attempted"});
        const auto checked = verify();
        if (!checked) return checked;
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(source_.value, FileDispositionInfo, &disposition, sizeof(disposition)))
            return Result<void>::failure(failure("transfer.removal_failed"));
        if (!CloseHandle(source_.value)) return Result<void>::failure(failure("transfer.removal_close"));
        source_.value = INVALID_HANDLE_VALUE;
        if (GetFileAttributesW(attempt_.source.c_str()) != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
            return Result<void>::failure({"transfer.removal_uncertain", "Source absence is not confirmed; final copy retained"});
        const auto finalHash = digest(destination_.value, {});
        const auto final = fingerprint(destination_.value);
        if (!finalHash || !final) return Result<void>::failure({"transfer.removal_final_unavailable", "Cannot confirm retained final copy"});
        auto actual = final.value();
        actual.sha256 = finalHash.value();
        if (!sameFingerprint(*attempt_.destinationFingerprint, actual))
            return Result<void>::failure({"transfer.removal_final_changed", "Final copy verification failed after source removal"});
        return Result<void>::success();
    }
};
} // namespace

// 확정 증거가 있는 두 파일만 독점 보호하는 함수.
Result<std::unique_ptr<ITransferRemovalSession>> WindowsTransferSourceRemoval::openImpl(const TransferAttempt& attempt, const MoveContract* movement) {
    using Return = Result<std::unique_ptr<ITransferRemovalSession>>;
    auto initial = attempt;
    initial.phase = TransferPhase::Prepared;
    initial.stagingFingerprint.reset(); initial.destinationFingerprint.reset(); initial.error.reset();
    initial.recoveryCompleted = false;
    initial.recoveryRemovalIntent = false;
    auto proof = attempt;
    // 저널 상태는 바꾸지 않고 같은 확정 증거의 파일 보호 조건만 재사용한다.
    if (attempt.recoveryRemovalIntent || attempt.phase != TransferPhase::DestinationCommitted) {
        auto eligible = attempt;
        eligible.recoveryRemovalIntent = false;
        if (!TransferValidation::recordRecoveryRemovalIntent(eligible))
            return Return::failure({"transfer.removal_evidence", "Recorded committed recovery evidence required"});
        proof.phase = TransferPhase::DestinationCommitted;
        proof.recoveryRemovalIntent = false;
    }
    const auto evidence = TransferValidation::advance(proof, {TransferPhase::DestinationCommitted, TransferPhase::SourceRemoveIntent});
    if (!TransferValidation::validateNew(initial) || !evidence ||
        !validPath(attempt.source) || !validPath(attempt.destination) || !validPath(attempt.staging) ||
        TransferValidation::sameObject(attempt.sourceFingerprint, *attempt.destinationFingerprint))
        return Return::failure({"transfer.removal_evidence", "Committed copy evidence with distinct objects required"});
    std::vector<Handle> parents;
    for (const auto& path : {attempt.source, attempt.destination}) {
        const auto locked = lockParents(path, parents);
        if (!locked) return Return::failure(locked.error());
    }
    Handle source(CreateFileW(attempt.source.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (source.value == INVALID_HANDLE_VALUE) return Return::failure(failure("transfer.removal_lock"));
    Handle destination(CreateFileW(attempt.destination.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (destination.value == INVALID_HANDLE_VALUE)
        return Return::failure(failure("transfer.removal_lock"));
    std::size_t volumeIndex = 0;
    for (const auto handle : {source.value, destination.value}) {
        const auto location = volume(handle);
        if (!location || !location.value().localNtfs)
            return Return::failure({"transfer.removal_volume", "Accessible local fixed NTFS required"});
        if (movement && (movement->strategy != MoveStrategy::VerifiedCopy ||
            movement->preservation != PreservationPolicy::LocalNtfsBasic ||
            !approvedVolume(location.value(), volumeIndex == 0 ? movement->sourceVolume : movement->destinationVolume)))
            return Return::failure({"transfer.volume_changed", "Protected removal volume differs from approval."});
        ++volumeIndex;
    }
    return Return::success(std::make_unique<RemovalSession>(std::move(parents), std::move(source), std::move(destination), attempt));
}

namespace {
// 없는 경로 끝을 제외하고 기존 안전한 조상의 볼륨을 조회하는 함수.
Result<TransferVolume> inspectVolumePath(const std::filesystem::path& path) {
    if (!validPath(path)) return Result<TransferVolume>::failure({"transfer.path", "Unsupported path"});
    auto ancestor = path;
    while (GetFileAttributesW(ancestor.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if ((error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) || ancestor == ancestor.root_path())
            return Result<TransferVolume>::failure(failure("transfer.volume"));
        ancestor = ancestor.parent_path();
    }
    std::vector<Handle> locks;
    const auto locked = lockParents(ancestor, locks);
    if (!locked) return Result<TransferVolume>::failure(locked.error());
    Handle handle(CreateFileW(ancestor.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info))
        return Result<TransferVolume>::failure(failure("transfer.volume"));
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return Result<TransferVolume>::failure({"transfer.reparse", "Reparse paths are unsupported"});
    return volume(handle.value);
}
} // namespace

// 공유된 읽기 전용 볼륨 검사를 호출하는 함수.
Result<TransferVolume> WindowsStagedCopy::inspectVolume(const std::filesystem::path& path) {
    return inspectVolumePath(path);
}

namespace {
// 보호된 부모 아래의 부재만 확인하고 접근 거부를 파일 없음으로 취급하지 않는 함수.
Result<void> requireMissing(const std::filesystem::path& path) {
    Handle probe(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (probe.value != INVALID_HANDLE_VALUE)
        return Result<void>::failure({"transfer.recovery_file_present", "Source or staging still exists; no cleanup is authorized"});
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
        return Result<void>::failure(failure("transfer.recovery_unavailable"));
    return Result<void>::success();
}

class RecoverySession final : public ITransferRecoverySession {
public:
    std::vector<Handle> parents;
    Handle destination;
    TransferAttempt attempt;
    // 최종 파일·부모 보호 핸들과 예상 전송 증거를 소유하는 함수.
    RecoverySession(std::vector<Handle> locks, Handle file, TransferAttempt expected)
        : parents(std::move(locks)), destination(std::move(file)), attempt(std::move(expected)) {}
    // 잠긴 최종 파일을 재검증하고 두 부재 관찰값을 완료 저장 직전에 확인하는 함수.
    Result<void> verify() override {
        const auto supported = ordinarySource(destination.value, attempt.destination);
        if (!supported) return supported;
        const auto before = fingerprint(destination.value);
        const auto hash = digest(destination.value, {});
        const auto after = fingerprint(destination.value);
        if (!before || !hash || !after)
            return Result<void>::failure(!before ? before.error() : (!hash ? hash.error() : after.error()));
        auto actual = after.value();
        actual.sha256 = hash.value();
        if (!sameFingerprint(before.value(), after.value()) || !attempt.destinationFingerprint ||
            !TransferValidation::sameObject(*attempt.destinationFingerprint, actual) ||
            !TransferValidation::sameContent(*attempt.destinationFingerprint, actual) ||
            attempt.destinationFingerprint->modifiedUtcMs != actual.modifiedUtcMs)
            return Result<void>::failure({"transfer.recovery_changed", "Final object, contents or modification time changed"});
        const auto source = requireMissing(attempt.source);
        if (!source) return source;
        return requireMissing(attempt.staging);
    }
};
}

// 파일을 생성하거나 제거하지 않고 승인 볼륨과 검증된 결과를 보호하는 함수.
Result<std::unique_ptr<ITransferRecoverySession>> WindowsTransferRecoveryGuard::open(
    const TransferAttempt& attempt, const MoveContract* movement) {
    using Return = Result<std::unique_ptr<ITransferRecoverySession>>;
    const auto evidence = TransferValidation::reconcileCompleted(attempt, true);
    if (!evidence) return Return::failure(evidence.error());
    auto initial = attempt;
    initial.phase = TransferPhase::Prepared;
    initial.stagingFingerprint.reset(); initial.destinationFingerprint.reset(); initial.error.reset();
    initial.recoveryCompleted = false;
    initial.recoveryRemovalIntent = false;
    if (!TransferValidation::validateNew(initial) || !validPath(attempt.source) ||
        !validPath(attempt.destination) || !validPath(attempt.staging))
        return Return::failure({"transfer.path", "Unsupported recovery paths"});
    std::vector<Handle> parents;
    const auto sourceParents = lockParents(attempt.source, parents);
    if (!sourceParents) return Return::failure(sourceParents.error());
    const auto sourceVolume = volume(parents.back().value);
    const auto targetParents = lockParents(attempt.destination, parents);
    if (!targetParents) return Return::failure(targetParents.error());
    const auto targetVolume = volume(parents.back().value);
    if (!sourceVolume || !targetVolume)
        return Return::failure(!sourceVolume ? sourceVolume.error() : targetVolume.error());
    if (!sourceVolume.value().localNtfs || !targetVolume.value().localNtfs ||
        sourceVolume.value().id != attempt.sourceFingerprint.volumeId ||
        targetVolume.value().id != attempt.destinationFingerprint->volumeId ||
        (movement && (movement->strategy != MoveStrategy::VerifiedCopy ||
        movement->preservation != PreservationPolicy::LocalNtfsBasic ||
        !approvedVolume(sourceVolume.value(), movement->sourceVolume) ||
        !approvedVolume(targetVolume.value(), movement->destinationVolume))))
        return Return::failure({"transfer.volume_changed", "Recovery volumes do not match recorded evidence and approval"});
    Handle finalFile(CreateFileW(attempt.destination.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (finalFile.value == INVALID_HANDLE_VALUE) return Return::failure(failure("transfer.recovery_open"));
    const auto finalVolume = volume(finalFile.value);
    if (!finalVolume || finalVolume.value().id != targetVolume.value().id ||
        (movement && !approvedVolume(finalVolume.value(), movement->destinationVolume)))
        return Return::failure({"transfer.volume_changed", "Final file volume changed"});
    return Return::success(std::make_unique<RecoverySession>(std::move(parents), std::move(finalFile), attempt));
}

namespace {
// 보호된 파일 핸들의 객체·전체 내용·수정 시각을 승인 지문과 대조하는 함수.
Result<void> verifyCleanupFile(HANDLE file, const std::filesystem::path& path, const FileFingerprint& expected) {
    const auto supported = ordinarySource(file, path);
    if (!supported) return supported;
    const auto hash = digest(file, {});
    const auto fp = fingerprint(file);
    if (!hash || !fp) return Result<void>::failure(!hash ? hash.error() : fp.error());
    auto actual = fp.value(); actual.sha256 = hash.value();
    if (!sameFingerprint(expected, actual))
        return Result<void>::failure({"transfer.cleanup_changed", "Source or verified staging object changed; no cleanup authorized"});
    return Result<void>::success();
}

class CleanupSession final : public IStagingCleanupSession {
    std::vector<Handle> parents_;
    Handle source_, staging_;
    TransferAttempt attempt_;
    bool attempted_{false};
public:
    // 삭제를 예약하지 않고 원본·임시 객체의 보호 핸들을 소유하는 함수.
    CleanupSession(std::vector<Handle> parents, Handle source, Handle staging, TransferAttempt attempt)
        : parents_(std::move(parents)), source_(std::move(source)), staging_(std::move(staging)), attempt_(std::move(attempt)) {}
    // 온전한 원본과 검증된 임시 복사본 또는 이전 정리 의도 뒤의 부재를 확인하는 함수.
    Result<void> verify() override {
        const auto source = verifyCleanupFile(source_.value, attempt_.source, attempt_.sourceFingerprint);
        if (!source) return source;
        const auto final = requireMissing(attempt_.destination);
        if (!final) return final;
        if (staging_.value == INVALID_HANDLE_VALUE) {
            if (attempt_.stagingCleanup != StagingCleanup::IntentRecorded)
                return Result<void>::failure({"transfer.cleanup_unrecorded_absence", "No prior cleanup intent proves this absence"});
            return requireMissing(attempt_.staging);
        }
        const auto staging = verifyCleanupFile(staging_.value, attempt_.staging, *attempt_.stagingFingerprint);
        if (!staging) return staging;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(staging_.value, &info)) return Result<void>::failure(failure("transfer.cleanup_stat"));
        if (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
            return Result<void>::failure({"transfer.cleanup_readonly", "Read-only staging is preserved; attributes are not changed"});
        return Result<void>::success();
    }
    // 저장된 정리 의도 뒤에 같은 임시 객체만 핸들로 제거하고 원본을 재확인하는 함수.
    Result<void> removeVerifiedStaging() override {
        if (std::exchange(attempted_, true)) return Result<void>::failure({"transfer.cleanup_reused", "Cleanup was already attempted"});
        const auto checked = verify();
        if (!checked) return checked;
        if (staging_.value != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            if (!SetFileInformationByHandle(staging_.value, FileDispositionInfo, &disposition, sizeof(disposition)))
                return Result<void>::failure(failure("transfer.cleanup_remove"));
            if (!CloseHandle(staging_.value)) return Result<void>::failure(failure("transfer.cleanup_close"));
            staging_.value = INVALID_HANDLE_VALUE;
        }
        const auto absent = requireMissing(attempt_.staging);
        if (!absent) return absent;
        const auto final = requireMissing(attempt_.destination);
        if (!final) return final;
        return verifyCleanupFile(source_.value, attempt_.source, attempt_.sourceFingerprint);
    }
};
}

// 원본 보존과 최종 경로 부재 아래 기록된 검증 임시 파일에만 삭제 권한을 여는 함수.
Result<std::unique_ptr<IStagingCleanupSession>> WindowsStagingCleanup::open(const TransferAttempt& attempt, const MoveContract* movement) {
    using Return = Result<std::unique_ptr<IStagingCleanupSession>>;
    const auto target = attempt.stagingCleanup == StagingCleanup::None ? StagingCleanup::IntentRecorded : StagingCleanup::Removed;
    const auto allowed = TransferValidation::recordStagingCleanup(attempt, target);
    if (!allowed) return Return::failure(allowed.error());
    auto initial = attempt;
    initial.phase = TransferPhase::Prepared; initial.stagingFingerprint.reset(); initial.destinationFingerprint.reset();
    initial.error.reset(); initial.recoveryCompleted = false; initial.stagingCleanup = StagingCleanup::None;
    if (!TransferValidation::validateNew(initial) || !validPath(attempt.source) ||
        !validPath(attempt.staging) || !validPath(attempt.destination))
        return Return::failure({"transfer.path", "Unsupported cleanup paths"});
    std::vector<Handle> parents;
    for (const auto& path : {attempt.source, attempt.staging}) {
        const auto locked = lockParents(path, parents);
        if (!locked) return Return::failure(locked.error());
    }
    Handle source(CreateFileW(attempt.source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (source.value == INVALID_HANDLE_VALUE) return Return::failure(failure("transfer.cleanup_source_lock"));
    const auto sourceVolume = volume(source.value);
    const auto stagingVolume = volume(parents.back().value);
    if (!sourceVolume || !stagingVolume || !sourceVolume.value().localNtfs || !stagingVolume.value().localNtfs ||
        sourceVolume.value().id != attempt.sourceFingerprint.volumeId || stagingVolume.value().id != attempt.stagingFingerprint->volumeId ||
        (movement && (movement->strategy != MoveStrategy::VerifiedCopy || movement->preservation != PreservationPolicy::LocalNtfsBasic ||
        !approvedVolume(sourceVolume.value(), movement->sourceVolume) || !approvedVolume(stagingVolume.value(), movement->destinationVolume))))
        return Return::failure({"transfer.volume_changed", "Protected cleanup volumes differ from recorded evidence or approval"});
    Handle staging(CreateFileW(attempt.staging.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (staging.value == INVALID_HANDLE_VALUE && (GetLastError() != ERROR_FILE_NOT_FOUND || attempt.stagingCleanup != StagingCleanup::IntentRecorded))
        return Return::failure(failure("transfer.cleanup_staging_lock"));
    if (staging.value != INVALID_HANDLE_VALUE) {
        const auto actualVolume = volume(staging.value);
        if (!actualVolume || actualVolume.value().id != stagingVolume.value().id ||
            (movement && !approvedVolume(actualVolume.value(), movement->destinationVolume)))
            return Return::failure({"transfer.volume_changed", "Staging object volume changed"});
    }
    return Return::success(std::make_unique<CleanupSession>(std::move(parents), std::move(source), std::move(staging), attempt));
}

// 파일을 생성하지 않고 보호된 핸들에서 객체·내용·실제 볼륨을 읽는 함수.
TransferFileObservation WindowsTransferFileInspector::inspect(const std::filesystem::path& path) {
    TransferFileObservation result;
    // 관찰 실패를 파일 없음으로 바꾸지 않고 이유를 보존하는 함수.
    const auto unavailable = [&](Error error) {
        result.presence = TransferPathPresence::Unavailable;
        result.error = std::move(error);
        return result;
    };
    const auto location = inspectVolumePath(path);
    if (!location) return unavailable(location.error());
    result.volumeId = location.value().id;
    if (!location.value().localNtfs)
        return unavailable({"transfer.volume_unsupported", "Only local fixed NTFS can be inspected"});
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return unavailable(failure("transfer.inspect"));
        result.presence = TransferPathPresence::Missing;
        return result;
    }
    std::vector<Handle> locks;
    const auto locked = lockParents(path, locks);
    if (!locked) return unavailable(locked.error());
    const auto parentVolume = volume(locks.back().value);
    if (!parentVolume) return unavailable(parentVolume.error());
    result.volumeId = parentVolume.value().id;
    Handle input(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            result.presence = TransferPathPresence::Missing;
            return result;
        }
        return unavailable(failure("transfer.inspect_open"));
    }
    const auto supported = ordinarySource(input.value, path);
    if (!supported) return unavailable(supported.error());
    const auto before = fingerprint(input.value);
    const auto hash = digest(input.value, {});
    const auto after = fingerprint(input.value);
    if (!before || !hash || !after)
        return unavailable(!before ? before.error() : (!hash ? hash.error() : after.error()));
    if (!sameFingerprint(before.value(), after.value()))
        return unavailable({"transfer.inspect_changed", "File changed while being inspected"});
    const auto checked = ordinarySource(input.value, path);
    if (!checked) return unavailable(checked.error());
    result.volumeId = after.value().volumeId;
    result.fingerprint = after.value();
    result.fingerprint->sha256 = hash.value();
    result.presence = TransferPathPresence::Present;
    return result;
}

// 지원 범위와 원본을 검사한 뒤 독점적인 새 임시 파일 세션을 만드는 함수.
Result<std::unique_ptr<IStagedCopySession>> WindowsStagedCopy::openImpl(
    const std::filesystem::path& source, const FileFingerprint& expected, const std::filesystem::path& staging, const MoveContract* movement) {
    using Return = Result<std::unique_ptr<IStagedCopySession>>;
    if (!validPath(source) || !validPath(staging) || staging.filename().empty() ||
        !expected.sha256 || expected.sha256->size() != 64 || !expected.volumeId || !expected.fileId)
        return Return::failure({"transfer.path", "Absolute ordinary paths and full source evidence required"});
    std::vector<Handle> locks;
    const auto sourceParents = lockParents(source, locks);
    if (!sourceParents) return Return::failure(sourceParents.error());
    Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    if (input.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input.value, &info) ||
        !GetFileInformationByHandleEx(input.value, FileBasicInfo, &basic, sizeof(basic)))
        return Return::failure(failure("transfer.source_unavailable"));
    const auto supported = ordinarySource(input.value, source);
    if (!supported) return Return::failure(supported.error());
    const auto sourceVolume = volume(input.value);
    if (!sourceVolume) return Return::failure(sourceVolume.error());
    if (!sourceVolume.value().localNtfs)
        return Return::failure({"transfer.volume_unsupported", "Only local fixed NTFS supported"});
    if (movement && (movement->strategy != MoveStrategy::VerifiedCopy ||
        movement->preservation != PreservationPolicy::LocalNtfsBasic ||
        !approvedVolume(sourceVolume.value(), movement->sourceVolume)))
        return Return::failure({"transfer.volume_changed", "Protected copy volume differs from approval."});
    auto fp = fingerprint(input.value);
    if (!fp) return Return::failure(fp.error());
    const auto hash = digest(input.value, {});
    if (!hash) return Return::failure(hash.error());
    fp.value().sha256 = hash.value();
    if (!sameFingerprint(expected, fp.value()))
        return Return::failure({"transfer.source_changed", "Source no longer matches approved evidence"});
    // 원본 객체·내용·계약을 확인하기 전에는 목적지 폴더를 생성하지 않는다.
    const auto targetParents = movement ? prepareApprovedParents(staging, movement->destinationVolume, locks) : lockParents(staging, locks);
    if (!targetParents) return Return::failure(targetParents.error());
    const auto targetVolume = volume(locks.back().value);
    if (!targetVolume) return Return::failure(targetVolume.error());
    if (!targetVolume.value().localNtfs)
        return Return::failure({"transfer.volume_unsupported", "Only local fixed NTFS supported"});
    if (movement && !approvedVolume(targetVolume.value(), movement->destinationVolume))
        return Return::failure({"transfer.volume_changed", "Protected copy destination differs from approval"});
    // DELETE 권한은 앱이 만든 임시 파일의 이름 변경에만 사용하며 제거 API는 제공하지 않는다.
    Handle output(CreateFileW(staging.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) return Return::failure(failure("transfer.staging_create"));
    const auto initial = fingerprint(output.value);
    if (!initial) return Return::failure(initial.error());
    return Return::success(std::make_unique<Session>(std::move(locks), std::move(input), std::move(output),
        expected, initial.value(), basic, source, staging));
}
} // namespace nookbit
#else
namespace nookbit {
// Windows 외에서는 보호 복구 완료 검사를 지원하지 않는 함수.
Result<std::unique_ptr<ITransferRecoverySession>> WindowsTransferRecoveryGuard::open(const TransferAttempt&, const MoveContract*) {
    return Result<std::unique_ptr<ITransferRecoverySession>>::failure({"transfer.unsupported", "Windows only"});
}
// Windows 외에서는 검증된 임시 복사본 정리를 지원하지 않는 함수.
Result<std::unique_ptr<IStagingCleanupSession>> WindowsStagingCleanup::open(const TransferAttempt&, const MoveContract*) {
    return Result<std::unique_ptr<IStagingCleanupSession>>::failure({"transfer.unsupported", "Windows only"});
}
// Windows 외에서는 원본 제거를 지원하지 않는 함수.
Result<std::unique_ptr<ITransferRemovalSession>> WindowsTransferSourceRemoval::openImpl(const TransferAttempt&, const MoveContract*) {
    return Result<std::unique_ptr<ITransferRemovalSession>>::failure({"transfer.unsupported", "Windows only"});
}
// Windows 외 플랫폼에서는 읽기 전용 검사도 지원하지 않는 함수.
TransferFileObservation WindowsTransferFileInspector::inspect(const std::filesystem::path&) {
    TransferFileObservation result;
    result.error = Error{"transfer.unsupported", "Windows only"};
    return result;
}
// Windows 외 플랫폼에서는 지원하지 않음을 반환하는 함수.
Result<TransferVolume> WindowsStagedCopy::inspectVolume(const std::filesystem::path&) {
    return Result<TransferVolume>::failure({"transfer.unsupported", "Windows only"});
}
// Windows 외 플랫폼에서는 파일을 생성하지 않는 함수.
Result<std::unique_ptr<IStagedCopySession>> WindowsStagedCopy::openImpl(
    const std::filesystem::path&, const FileFingerprint&, const std::filesystem::path&, const MoveContract*) {
    return Result<std::unique_ptr<IStagedCopySession>>::failure({"transfer.unsupported", "Windows only"});
}
} // namespace nookbit
#endif
