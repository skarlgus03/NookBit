#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// Qt의 SHA-256 구현을 코어 해시 포트에 연결하는 어댑터.
class QtPlanHasher final : public IPlanHasher {
public:
    /// 입력 바이트의 소문자 16진 SHA-256을 계산하는 함수.
    [[nodiscard]] std::string sha256(std::string_view bytes) const override;
};

} // namespace nookbit
