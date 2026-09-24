#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 분류 규칙을 파일 정보와 비교하는 코어 구현체.
class RuleEngine final : public IRuleEngine {
public:
    /// 활성 규칙을 평가해 가장 우선하는 판단 결과를 반환하는 함수.
    [[nodiscard]] Result<std::optional<RuleDecision>> decide(
        const FileFact& file,
        const RuleSet& rules,
        MultiMatchPolicy multiMatchPolicy,
        std::int64_t nowUtcMs) const override;
};

} // namespace nookbit
