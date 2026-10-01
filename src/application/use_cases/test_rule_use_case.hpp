#pragma once

#include <nookbit/application/ports.hpp>

namespace nookbit {

/// 파일을 변경하지 않고 규칙 초안을 시험하도록 입력 수집과 판정을 조율한다.
class TestRuleUseCase final {
public:
    /// 파일 스캐너와 규칙 경로 검사자를 연결하는 생성자.
    TestRuleUseCase(IScanner& scanner, const IRuleTracer& tracer);

    /// 선택한 실제 파일 하나의 메타데이터를 읽어 규칙을 시험하는 함수.
    [[nodiscard]] Result<RuleTrace> testSelectedFile(const std::filesystem::path& filePath,
                                                      const RuleSet& rules,
                                                      std::int64_t nowUtcMs) const;
    /// 화면에서 만든 가상 파일 메타데이터로 규칙을 시험하는 함수.
    [[nodiscard]] Result<RuleTrace> testMetadata(const FileFact& file, const RuleSet& rules,
                                                 std::int64_t nowUtcMs) const;

private:
    IScanner& scanner_;
    const IRuleTracer& tracer_;
};

} // namespace nookbit
