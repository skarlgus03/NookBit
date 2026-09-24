#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 직접 선택한 파일과 감시 폴더를 별도 방식으로 읽는 스캐너 구현체.
class FilesystemScanner final : public IScanner {
public:
    /// 파일을 변경하지 않고 스캔 결과를 반환하는 함수.
    [[nodiscard]] Result<ScanSnapshot> scan(const ScanRequest& request) override;
};

} // namespace nookbit
