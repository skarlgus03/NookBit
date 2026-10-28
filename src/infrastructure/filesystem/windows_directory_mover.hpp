#pragma once
#include <nookbit/application/ports.hpp>
namespace nookbit {
/// 폴더 묶음의 볼륨 간 복사·확정·원본 제거를 담당하는 파일 시스템 일꾼.
class WindowsDirectoryMover final {
public:
    /// 원본과 목적지를 덮어쓰지 않고 승인된 폴더 전체를 옮기는 함수.
    static Result<void> move(const std::filesystem::path& source,
        const std::filesystem::path& destination, const FileFingerprint& expected,
        IScanner& scanner, const IPlanHasher& hasher);
};
}
