#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {
/// 새 미리보기에도 기존의 같은 볼륨 검사 정책만 유지하는 함수.
void inheritMetadataRenamePolicy(OperationPlan& current, const OperationPlan& reference);

/// 이동 예정 파일의 내용 해시를 재검사해 계획에 붙이는 함수.
[[nodiscard]] Result<void> attachContentHashes(
    OperationPlan& plan, IScanner& scanner, IPlanHasher& hasher);

} // namespace nookbit
