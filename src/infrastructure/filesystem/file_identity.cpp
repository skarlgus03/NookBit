#include "infrastructure/filesystem/file_identity.hpp"

#include <system_error>
#include <utility>

namespace nookbit {

// 실행 계획의 파일 ID·크기·수정 시각과 현재 파일이 같은지 검사하는 함수.
bool sameFingerprint(const FileFingerprint& expected, const FileFingerprint& actual) {
    if (expected.directory != actual.directory) return false;
    // 볼륨 간 복사된 폴더는 객체 ID가 달라지므로 승인된 전체 묶음의 내용으로 비교한다.
    if (expected.directory && expected.sha256)
        return expected.sha256 == actual.sha256;
    return expected.volumeId && expected.fileId && actual.volumeId && actual.fileId &&
           expected.volumeId == actual.volumeId && expected.fileId == actual.fileId &&
           expected.size == actual.size && expected.modifiedUtcMs == actual.modifiedUtcMs &&
           (!expected.sha256 || expected.sha256 == actual.sha256);
}

// 한 경로를 스캔해 현재 파일 지문을 반환하는 함수.
Result<FileFingerprint> scanFingerprint(IScanner& scanner, const std::filesystem::path& path,
                                        const IPlanHasher* hasher) {
    auto scanned = scanner.scan({{}, false, {}, {path}});
    if (!scanned) return Result<FileFingerprint>::failure(scanned.error());
    if (scanned.value().files.size() != 1) {
        return Result<FileFingerprint>::failure({
            "identity.file_missing", "Expected exactly one accessible selected file.",
        });
    }
    auto fingerprint = scanned.value().files.front().fingerprint;
    if (hasher) {
        auto digest = hasher->sha256File(path);
        if (!digest) return Result<FileFingerprint>::failure(digest.error());
        auto checked = scanner.scan({{}, false, {}, {path}});
        if (!checked || checked.value().files.size() != 1 ||
            !sameFingerprint(fingerprint, checked.value().files.front().fingerprint)) {
            return Result<FileFingerprint>::failure({
                "identity.changed_during_hash", "File changed while its content was hashed.",
            });
        }
        fingerprint.sha256 = std::move(digest.value());
    }
    return Result<FileFingerprint>::success(std::move(fingerprint));
}

// 파일이 없거나, 같은 파일이거나, 다른 파일인지 구분하는 함수.
FileProbe probeFile(IScanner& scanner,
                    const std::filesystem::path& path,
                    const FileFingerprint& expected,
                    const IPlanHasher* hasher) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return FileProbe::Missing;
    if (error) return FileProbe::Unavailable;
    if (!std::filesystem::exists(status)) return FileProbe::Missing;
    if (std::filesystem::is_symlink(status) ||
        (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status))) return FileProbe::Unavailable;
    auto fingerprint = scanFingerprint(scanner, path, expected.sha256 ? hasher : nullptr);
    if (!fingerprint) return FileProbe::Unavailable;
    return sameFingerprint(expected, fingerprint.value())
        ? FileProbe::Matches : FileProbe::Different;
}

} // namespace nookbit
