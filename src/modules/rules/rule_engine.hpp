#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 분류 규칙을 파일 정보와 비교하는 코어 구현체.
class RuleEngine final : public IRuleEngine, public IRuleTracer, public IProcessingFlowRouter {
public:
    /// 명시적으로 고른 처리 그래프에서 다음 순회 또는 분류 결과를 반환하는 함수.
    Result<ProcessingRoute> route(const FileFact& file, const RuleSet& rules,
                                   std::int64_t nowUtcMs) const override;
    /// 활성 규칙을 평가해 가장 우선하는 판단 결과를 반환하는 함수.
    [[nodiscard]] Result<std::optional<RuleDecision>> decide(
        const FileFact& file,
        const RuleSet& rules,
        MultiMatchPolicy multiMatchPolicy,
        std::int64_t nowUtcMs) const override;

    /// 실제 분류와 같은 경로로 v3 규칙을 평가하고 지나간 노드를 반환하는 함수.
    [[nodiscard]] Result<RuleTrace> trace(const FileFact& file, const RuleSet& rules,
                                          std::int64_t nowUtcMs) const override;
};

} // namespace nookbit
