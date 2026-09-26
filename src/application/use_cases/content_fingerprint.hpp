#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 이동 예정 파일의 내용 해시를 재검사해 계획에 붙이는 함수.
[[nodiscard]] Result<void> attachContentHashes(
    OperationPlan& plan, IScanner& scanner, IPlanHasher& hasher);

} // namespace nookbit
