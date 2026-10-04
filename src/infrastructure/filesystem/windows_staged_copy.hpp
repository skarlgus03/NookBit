#pragma once

#include <nookbit/application/staged_copy.hpp>

namespace nookbit {

/// 원본 제거 API 없이 로컬 NTFS의 임시 복사·검증·최종 이름 확정을 수행하는 어댑터.
class WindowsStagedCopy final : public IStagedCopy {
public:
    /// 기존 경로의 볼륨과 지원 여부를 조회하는 함수.
    [[nodiscard]] Result<TransferVolume> inspectVolume(const std::filesystem::path& path) override;
    /// 원본과 부모 경로를 잠그고 새 임시 파일을 생성하는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> open(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging) override;
    /// 승인 볼륨을 보호된 핸들에서 확인한 후 새 임시 파일을 만드는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openApproved(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging, const MoveContract& movement) override;
private:
    /// 승인 계약 유무에 따라 동일한 보호 복사 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openImpl(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging, const MoveContract* movement);
};

} // namespace nookbit
