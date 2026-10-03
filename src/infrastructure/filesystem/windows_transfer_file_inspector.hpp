#pragma once
#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 생성·이동·삭제 없이 보호된 핸들로 전송 복구용 파일 정보를 읽는 어댑터.
class WindowsTransferFileInspector final : public ITransferFileInspector {
public:
    /// 볼륨과 파일 ID·내용을 안전하게 읽고 확인 불가 이유도 반환하는 함수.
    [[nodiscard]] TransferFileObservation inspect(const std::filesystem::path& path) override;
};
} // namespace nookbit
