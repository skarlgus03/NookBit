#include "application/use_cases/plan_signature.hpp"

#include <algorithm>
#include <string_view>

namespace nookbit {
namespace {

// 길이 접두사를 붙여 필드 경계가 모호하지 않게 추가하는 함수.
void append(std::string& output, std::string_view value) {
    output += std::to_string(value.size());
    output += ':';
    output.append(value);
}

// 경로의 UTF-8 표현을 고정 형식에 추가하는 함수.
void appendPath(std::string& output, const std::filesystem::path& path) {
    const auto value = path.lexically_normal().generic_u8string();
    append(output, {reinterpret_cast<const char*>(value.data()), value.size()});
}

// 정수 또는 열거형 값을 고정 형식에 추가하는 함수.
template<typename T>
void appendNumber(std::string& output, T value) {
    append(output, std::to_string(static_cast<long long>(value)));
}

// 선택값의 존재 여부와 값을 고정 형식에 추가하는 함수.
template<typename T>
void appendOptionalNumber(std::string& output, const std::optional<T>& value) {
    appendNumber(output, value.has_value());
    if (value) appendNumber(output, *value);
}

// 선택 문자열의 존재 여부와 값을 고정 형식에 추가하는 함수.
void appendOptionalString(std::string& output, const std::optional<std::string>& value) {
    appendNumber(output, value.has_value());
    if (value) append(output, *value);
}

} // namespace

// 사용자 설정 전체를 해시할 수 있는 고정 형식으로 직렬화하는 함수.
std::string PlanSignature::settings(const UserSettings& settings) {
    std::string output = "nookbit-settings-v1";
    appendPath(output, settings.homeRoot);
    appendNumber(output, settings.watchRoots.size());
    for (const auto& root : settings.watchRoots) appendPath(output, root);
    appendNumber(output, settings.excludePatterns.size());
    for (const auto& pattern : settings.excludePatterns) append(output, pattern);
    appendNumber(output, settings.defaultCollisionPolicy);
    appendNumber(output, settings.multiMatchPolicy);
    appendNumber(output, settings.unmatchedPolicy);
    appendNumber(output, settings.recursiveScan);
    return output;
}

// 규칙 내용 전체를 해시할 수 있는 고정 형식으로 직렬화하는 함수.
std::string PlanSignature::rules(const RuleSet& rules) {
    std::string output = "nookbit-rules-v1";
    append(output, rules.id);
    append(output, rules.version);
    appendNumber(output, rules.rules.size());
    for (const auto& rule : rules.rules) {
        append(output, rule.id);
        appendNumber(output, rule.enabled);
        appendNumber(output, rule.priority);
        appendNumber(output, rule.extensions.size());
        for (const auto& extension : rule.extensions) append(output, extension);
        appendNumber(output, rule.fileNameGlobs.size());
        for (const auto& glob : rule.fileNameGlobs) append(output, glob);
        appendOptionalNumber(output, rule.minimumBytes);
        appendOptionalNumber(output, rule.maximumBytes);
        appendOptionalNumber(output, rule.minimumAgeDays);
        appendOptionalNumber(output, rule.maximumAgeDays);
        appendPath(output, rule.destinationRelativePath);
        appendOptionalNumber(output, rule.collisionPolicy);
    }
    return output;
}

// 파일 경로·지문·판단 결과·경고를 고정 형식으로 직렬화하는 함수.
std::string PlanSignature::planContents(const OperationPlan& plan) {
    std::string output = "nookbit-plan-contents-v1";
    append(output, plan.ruleSetVersion);
    appendNumber(output, plan.steps.size());
    for (const auto& step : plan.steps) {
        appendPath(output, step.source);
        appendPath(output, step.destination);
        appendNumber(output, step.fingerprint.size);
        appendNumber(output, step.fingerprint.modifiedUtcMs);
        appendOptionalString(output, step.fingerprint.volumeId);
        appendOptionalString(output, step.fingerprint.fileId);
        appendOptionalString(output, step.fingerprint.sha256);
        append(output, step.matchedRuleId);
        append(output, step.reason);
        appendNumber(output, step.collisionPolicy);
        appendNumber(output, step.status);
    }
    appendNumber(output, plan.warnings.size());
    for (const auto& warning : plan.warnings) {
        append(output, warning.code);
        append(output, warning.message);
    }
    return output;
}

// 저장된 계획의 변조 검출 해시에 사용할 입력을 만드는 함수.
std::string PlanSignature::approvalPayload(const OperationPlan& plan) {
    OperationPlan approvalSnapshot = plan;
    for (auto& step : approvalSnapshot.steps) {
        if (step.status != StepStatus::Skipped) step.status = StepStatus::Planned;
    }
    return "nookbit-approval-plan-v1:" + plan.id + ":" + plan.settingsRevision +
           ":" + plan.rulesRevision + ":" + planContents(approvalSnapshot);
}

// 소문자 16진 SHA-256 형식인지 검사하는 함수.
bool PlanSignature::isSha256Hex(const std::string& digest) {
    return digest.size() == 64 &&
           std::all_of(digest.begin(), digest.end(), [](char value) {
               return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
           });
}

} // namespace nookbit
