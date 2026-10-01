#include "application/use_cases/test_rule_use_case.hpp"

namespace nookbit {

// 주입받은 파일 스캐너와 규칙 경로 검사자를 보관하는 생성자.
TestRuleUseCase::TestRuleUseCase(IScanner& scanner, const IRuleTracer& tracer)
    : scanner_(scanner), tracer_(tracer) {}

// 실제 파일을 한 개만 읽고 그 메타데이터로 규칙을 시험하는 함수.
Result<RuleTrace> TestRuleUseCase::testSelectedFile(const std::filesystem::path& filePath,
                                                    const RuleSet& rules,
                                                    std::int64_t nowUtcMs) const {
    ScanRequest request;
    request.selectedFiles.push_back(filePath);
    auto scanned = scanner_.scan(request);
    if (!scanned) return Result<RuleTrace>::failure(scanned.error());
    if (scanned.value().files.size() != 1) return Result<RuleTrace>::failure({
        "rule_test.file_unavailable", "Cannot inspect exactly one selected file."});
    return testMetadata(scanned.value().files.front(), rules, nowUtcMs);
}

// 파일 변경 없이 메타데이터를 기존 분류 엔진의 경로 검사에 전달하는 함수.
Result<RuleTrace> TestRuleUseCase::testMetadata(const FileFact& file, const RuleSet& rules,
                                                std::int64_t nowUtcMs) const {
    return tracer_.trace(file, rules, nowUtcMs);
}

} // namespace nookbit
