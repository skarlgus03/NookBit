#include "infrastructure/filesystem/file_identity.hpp"

#include <system_error>

namespace nookbit {

// 실행 계획의 파일 ID·크기·수정 시각과 현재 파일이 같은지 검사하는 함수.
bool sameFingerprint(const FileFingerprint& expected, const FileFingerprint& actual) {
    return expected.volumeId && expected.fileId && actual.volumeId && actual.fileId &&
           expected.volumeId == actual.volumeId && expected.fileId == actual.fileId &&
           expected.size == actual.size && expected.modifiedUtcMs == actual.modifiedUtcMs &&
           (!expected.sha256 || expected.sha256 == actual.sha256);
}

// 한 경로를 스캔해 현재 파일 지문을 반환하는 함수.
Result<FileFingerprint> scanFingerprint(IScanner& scanner, const std::filesystem::path& path) {
    auto scanned = scanner.scan({{}, false, {}, {path}});
    if (!scanned) return Result<FileFingerprint>::failure(scanned.error());
    if (scanned.value().files.size() != 1) {
        return Result<FileFingerprint>::failure({
            "identity.file_missing", "Expected exactly one accessible selected file.",
        });
    }
    return Result<FileFingerprint>::success(scanned.value().files.front().fingerprint);
}

// 파일이 없거나, 같은 파일이거나, 다른 파일인지 구분하는 함수.
FileProbe probeFile(IScanner& scanner,
                    const std::filesystem::path& path,
                    const FileFingerprint& expected) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) return FileProbe::Missing;
    if (error) return FileProbe::Unavailable;
    if (!std::filesystem::exists(status)) return FileProbe::Missing;
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) return FileProbe::Unavailable;
    auto fingerprint = scanFingerprint(scanner, path);
    if (!fingerprint) return FileProbe::Unavailable;
    return sameFingerprint(expected, fingerprint.value())
        ? FileProbe::Matches : FileProbe::Different;
}

} // namespace nookbit
