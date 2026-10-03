#include "windows_staged_copy.hpp"
#include "windows_transfer_file_inspector.hpp"
#include "windows_transfer_source_removal.hpp"
#include "file_identity.hpp"
#include <nookbit/domain/transfer.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
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

// 우회 경로·대체 스트림·네트워크 경로를 받지 않는 함수.
bool validPath(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.root_name().wstring().size() != 2 ||
        path.native().find(wchar_t{}) != std::wstring::npos) return false;
    for (const auto& part : path.relative_path()) {
        const auto text = part.wstring();
        if (text == L".." || text == L"." || text.find(L':') != std::wstring::npos ||
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
        Handle handle(CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES,
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
        const auto name = destination.lexically_normal().wstring();
        const size_t bytes = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
        if (bytes > MAXDWORD)
            return Result<FileFingerprint>::failure({"transfer.commit_path", "Destination path too long"});
        std::vector<uint64_t> storage((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        rename->ReplaceIfExists = FALSE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::copy(name.begin(), name.end(), rename->FileName);
        if (!SetFileInformationByHandle(staging.value, FileRenameInfo, rename, static_cast<DWORD>(bytes)))
            return Result<FileFingerprint>::failure(failure("transfer.commit_rename"));
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
Result<std::unique_ptr<ITransferRemovalSession>> WindowsTransferSourceRemoval::open(const TransferAttempt& attempt) {
    using Return = Result<std::unique_ptr<ITransferRemovalSession>>;
    auto initial = attempt;
    initial.phase = TransferPhase::Prepared;
    initial.stagingFingerprint.reset(); initial.destinationFingerprint.reset(); initial.error.reset();
    const auto evidence = TransferValidation::advance(attempt, {TransferPhase::DestinationCommitted, TransferPhase::SourceRemoveIntent});
    if (attempt.direction != TransferDirection::Forward || !TransferValidation::validateNew(initial) || !evidence ||
        !validPath(attempt.source) || !validPath(attempt.destination) || !validPath(attempt.staging) ||
        TransferValidation::sameObject(attempt.sourceFingerprint, *attempt.destinationFingerprint))
        return Return::failure({"transfer.removal_evidence", "Forward committed copy evidence with distinct objects required"});
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
    for (const auto handle : {source.value, destination.value}) {
        const auto location = volume(handle);
        if (!location || !location.value().localNtfs)
            return Return::failure({"transfer.removal_volume", "Accessible local fixed NTFS required"});
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
Result<std::unique_ptr<IStagedCopySession>> WindowsStagedCopy::open(
    const std::filesystem::path& source, const FileFingerprint& expected, const std::filesystem::path& staging) {
    using Return = Result<std::unique_ptr<IStagedCopySession>>;
    if (!validPath(source) || !validPath(staging) || staging.filename().empty() ||
        !expected.sha256 || expected.sha256->size() != 64 || !expected.volumeId || !expected.fileId)
        return Return::failure({"transfer.path", "Absolute ordinary paths and full source evidence required"});
    std::vector<Handle> locks;
    for (const auto& path : {source, staging}) {
        const auto locked = lockParents(path, locks);
        if (!locked) return Return::failure(locked.error());
    }
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
    const auto targetVolume = volume(locks.back().value);
    if (!sourceVolume || !targetVolume) return Return::failure(!sourceVolume ? sourceVolume.error() : targetVolume.error());
    if (!sourceVolume.value().localNtfs || !targetVolume.value().localNtfs)
        return Return::failure({"transfer.volume_unsupported", "Only local fixed NTFS supported"});
    auto fp = fingerprint(input.value);
    if (!fp) return Return::failure(fp.error());
    const auto hash = digest(input.value, {});
    if (!hash) return Return::failure(hash.error());
    fp.value().sha256 = hash.value();
    if (!sameFingerprint(expected, fp.value()))
        return Return::failure({"transfer.source_changed", "Source no longer matches approved evidence"});
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
// Windows 외에서는 원본 제거를 지원하지 않는 함수.
Result<std::unique_ptr<ITransferRemovalSession>> WindowsTransferSourceRemoval::open(const TransferAttempt&) {
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
Result<std::unique_ptr<IStagedCopySession>> WindowsStagedCopy::open(
    const std::filesystem::path&, const FileFingerprint&, const std::filesystem::path&) {
    return Result<std::unique_ptr<IStagedCopySession>>::failure({"transfer.unsupported", "Windows only"});
}
} // namespace nookbit
#endif
