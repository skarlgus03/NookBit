#pragma once

#include <nookbit/application/staged_copy.hpp>

namespace nookbit {

/// 원본 제거 API 없이 로컬 NTFS의 임시 복사·검증·최종 이름 확정을 수행하는 어댑터.
class WindowsStagedCopy final : public IStagedCopy {
public:
    /// 원본 해시가 승인과 일치해야 임시 파일을 만들고 보호 핸들을 유지하는지 반환하는 함수.
    [[nodiscard]] bool verifiesSourceBeforeStaging() const override {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }
    /// 원본을 수정하거나 전체 내용을 읽지 않고 지원 여부와 다운로드 표식 유무를 검사하는 함수.
    [[nodiscard]] Result<bool> preflightSource(const std::filesystem::path& source);
    /// 전체 검증된 재개 복사본을 보호 재검사해 확정 세션만 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openVerified(
        const TransferAttempt& attempt, const MoveContract* movement, const std::function<bool()>& cancelled = {}) override;
    /// 기존 임시 객체와 원본의 앞부분을 비교하고 보호된 이어쓰기 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openResume(
        const TransferAttempt& attempt, const MoveContract* movement, const std::function<bool()>& cancelled = {}) override;
    /// 기존 경로의 볼륨과 지원 여부를 조회하는 함수.
    [[nodiscard]] Result<TransferVolume> inspectVolume(const std::filesystem::path& path) override;
    /// 원본과 부모 경로를 잠그고 새 임시 파일을 생성하는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> open(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging) override;
    /// 원본·승인 볼륨을 검증하고 없는 부모를 안전하게 생성한 뒤 새 임시 파일을 만드는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openApproved(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging, const MoveContract& movement) override;
private:
    /// 기존 임시 파일을 변경 없이 검사해 이어쓰기 또는 확정 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openExistingImpl(
        const TransferAttempt& attempt, const MoveContract* movement, const std::function<bool()>& cancelled, bool verified);
    /// 승인 계약 유무에 따라 동일한 보호 복사 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagedCopySession>> openImpl(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging, const MoveContract* movement);
};

} // namespace nookbit
