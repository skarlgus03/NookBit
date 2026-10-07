#pragma once
#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 생성·이동·삭제 없이 보호된 핸들로 전송 복구용 파일 정보를 읽는 어댑터.
class WindowsTransferFileInspector final : public ITransferFileInspector {
public:
    /// 볼륨과 파일 ID·내용을 안전하게 읽고 확인 불가 이유도 반환하는 함수.
    [[nodiscard]] TransferFileObservation inspect(const std::filesystem::path& path) override;
    /// Zone.Identifier를 읽기 전용으로 허용하는 완료 작업 검사 함수.
    [[nodiscard]] TransferFileObservation inspectWithZone(const std::filesystem::path& path) override;
    /// 보호된 최종 파일의 표식·원래 속성을 제거 영수증과 대조하는 함수.
    [[nodiscard]] Result<void> checkRemovalProof(const std::filesystem::path& path, const TransferRemovalProof& proof) override;
    /// 보호된 두 파일의 표식 원문을 변경 없이 대조하는 함수.
    [[nodiscard]] Result<void> checkDownloadMarkers(const std::filesystem::path& source,
        const std::filesystem::path& copy, bool allowMissingCopy = false) override;
private:
    /// 계약별 지원 범위를 적용하는 읽기 전용 검사 함수.
    [[nodiscard]] TransferFileObservation inspectImpl(const std::filesystem::path& path, bool allowZone);
};
} // namespace nookbit
