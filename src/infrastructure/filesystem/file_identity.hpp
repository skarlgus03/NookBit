#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

enum class FileProbe {
    Missing,
    Matches,
    Different,
    Unavailable,
};

/// 실행 계획의 파일 ID·크기·수정 시각과 현재 파일이 같은지 검사하는 함수.
[[nodiscard]] bool sameFingerprint(
    const FileFingerprint& expected,
    const FileFingerprint& actual);

/// 한 경로를 스캔해 현재 파일 지문을 반환하는 함수.
[[nodiscard]] Result<FileFingerprint> scanFingerprint(
    IScanner& scanner,
    const std::filesystem::path& path);

/// 파일이 없거나, 같은 파일이거나, 다른 파일인지 구분하는 함수.
[[nodiscard]] FileProbe probeFile(
    IScanner& scanner,
    const std::filesystem::path& path,
    const FileFingerprint& expected);

} // namespace nookbit
