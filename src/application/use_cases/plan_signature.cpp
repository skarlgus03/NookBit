#include "application/use_cases/plan_signature.hpp"

#include <cwctype>

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

// 트리 조건을 순서와 선택값까지 포함해 서명 문자열에 추가하는 함수.
void appendCondition(std::string& output, const RuleCondition& condition) {
    appendNumber(output, condition.extensions.size());
    for (const auto& extension : condition.extensions) append(output, extension);
    appendNumber(output, condition.fileNameGlobs.size());
    for (const auto& glob : condition.fileNameGlobs) append(output, glob);
    appendOptionalNumber(output, condition.minimumBytes);
    appendOptionalNumber(output, condition.maximumBytes);
    appendOptionalNumber(output, condition.greaterThanBytes);
    appendOptionalNumber(output, condition.minimumAgeDays);
    appendOptionalNumber(output, condition.maximumAgeDays);
}

// v3 단일 조건의 종류와 값을 서명 문자열에 추가하는 함수.
void appendAtomicCondition(std::string& output, const AtomicRuleCondition& condition) {
    appendNumber(output, condition.index());
    if (const auto* extension = std::get_if<ExtensionPredicate>(&condition)) {
        appendNumber(output, extension->values.size());
        for (const auto& value : extension->values) append(output, value);
    } else if (const auto* name = std::get_if<NameContainsPredicate>(&condition)) {
        append(output, name->text);
    } else if (const auto* size = std::get_if<SizePredicate>(&condition)) {
        appendNumber(output, size->comparison);
        appendNumber(output, size->bytes);
    } else {
        const auto& age = std::get<ModifiedAgePredicate>(condition);
        appendNumber(output, age.comparison);
        appendNumber(output, age.days);
    }
}

// 선택한 목적지와 충돌 정책을 서명 문자열에 추가하는 함수.
void appendDestination(std::string& output, const RuleDestination& destination) {
    appendPath(output, destination.relativePath);
    appendOptionalNumber(output, destination.collisionPolicy);
}

// 트리 노드와 자식을 화면 순서대로 서명 문자열에 추가하는 함수.
void appendNode(std::string& output, const RuleNode& node) {
    append(output, node.id);
    appendNumber(output, node.enabled);
    appendNumber(output, node.otherwise);
    appendCondition(output, node.condition);
    appendNumber(output, node.destination.has_value());
    if (node.destination) appendDestination(output, *node.destination);
    appendNumber(output, node.children.size());
    for (const auto& child : node.children) appendNode(output, child);
}

// v3 노드의 단일 조건과 자식을 순서대로 서명에 추가하는 함수.
void appendAtomicNode(std::string& output, const RuleNode& node) {
    append(output, node.id);
    appendNumber(output, node.enabled);
    appendNumber(output, node.otherwise);
    appendNumber(output, node.atomicCondition.has_value());
    if (node.atomicCondition) appendAtomicCondition(output, *node.atomicCondition);
    appendNumber(output, node.destination.has_value());
    if (node.destination) appendDestination(output, *node.destination);
    appendNumber(output, node.exclude);
    appendNumber(output, node.children.size());
    for (const auto& child : node.children) appendAtomicNode(output, child);
}

} // namespace

// 버전별 필수 정보와 실제 볼륨에 따른 이동 방식의 일관성을 검사하는 함수.
Result<void> PlanSignature::validateFormat(const OperationPlan& plan) {
    if (plan.formatVersion != 1 && plan.formatVersion != 2)
        return Result<void>::failure({"plan.format_unsupported", "Unsupported approval plan version."});
    // GUID 볼륨 경로와 32비트 십진 일련번호가 정규 형식인지 확인하는 함수.
    const auto validVolume = [](const PlanVolume& volume) {
        const auto& guid = volume.guidPath;
        if (guid.size() != 49 || guid.substr(0, 11) != "\\\\?\\Volume{" || guid.substr(47) != "}\\" ||
            volume.filesystem != "NTFS" || volume.serial.empty() || volume.serial.size() > 10 ||
            (volume.serial.size() > 1 && volume.serial.front() == '0')) return false;
        for (std::size_t i = 11; i < 47; ++i) {
            const auto offset = i - 11;
            if (offset == 8 || offset == 13 || offset == 18 || offset == 23) {
                if (guid[i] != '-') return false;
            } else if (!((guid[i] >= '0' && guid[i] <= '9') || (guid[i] >= 'a' && guid[i] <= 'f'))) return false;
        }
        if (!std::all_of(volume.serial.begin(), volume.serial.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return false;
        return volume.serial.size() < 10 || volume.serial <= "4294967295";
    };
    for (const auto& step : plan.steps) {
        if (plan.formatVersion == 1) {
            if (step.movement) return Result<void>::failure({"plan.contract_unsigned", "Legacy plans cannot carry unsigned movement contracts."});
            continue;
        }
        if (!step.movement) return Result<void>::failure({"plan.contract_missing", "Every v2 step needs a movement contract."});
        const auto& movement = *step.movement;
        if (!validVolume(movement.sourceVolume) || !validVolume(movement.destinationVolume) ||
            !step.fingerprint.volumeId || *step.fingerprint.volumeId != movement.sourceVolume.serial ||
            movement.preservation != PreservationPolicy::LocalNtfsBasic)
            return Result<void>::failure({"plan.volume_invalid", "Volume identity or preservation policy is invalid."});
        const bool same = movement.sourceVolume.guidPath == movement.destinationVolume.guidPath;
        if ((same && movement.sourceVolume.serial != movement.destinationVolume.serial) ||
            movement.strategy != (same ? MoveStrategy::Rename : MoveStrategy::VerifiedCopy))
            return Result<void>::failure({"plan.strategy_invalid", "Movement strategy does not match the approved volumes."});
    }
    return Result<void>::success();
}

// 구형 계획을 복사 전략으로 암묵적으로 확장하지 않는 함수.
Result<void> PlanSignature::validateMovementScope(const OperationPlan& plan) {
    if (plan.formatVersion != 1) return Result<void>::success();
    for (const auto& step : plan.steps) {
        auto source = step.source.root_name().wstring();
        auto destination = step.destination.root_name().wstring();
#ifdef _WIN32
        // Windows 드라이브 이름의 대소문자를 정규화하는 함수.
        const auto lower = [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); };
        std::transform(source.begin(), source.end(), source.begin(), lower);
        std::transform(destination.begin(), destination.end(), destination.begin(), lower);
#endif
        if (source != destination)
            return Result<void>::failure({"plan.volume_contract_required", "Cross-drive drafts require explicit v2 volume contracts."});
    }
    return Result<void>::success();
}

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
    if (rules.schemaVersion == 3) {
        std::string output = "nookbit-rules-v3";
        append(output, rules.id);
        append(output, rules.version);
        appendNumber(output, rules.nodes.size());
        for (const auto& node : rules.nodes) appendAtomicNode(output, node);
        appendNumber(output, rules.globalOtherwise.has_value());
        if (rules.globalOtherwise) appendDestination(output, *rules.globalOtherwise);
        return output;
    }
    if (rules.schemaVersion == 2) {
        std::string output = "nookbit-rules-v2";
        append(output, rules.id);
        append(output, rules.version);
        appendNumber(output, rules.nodes.size());
        for (const auto& node : rules.nodes) appendNode(output, node);
        appendNumber(output, rules.globalOtherwise.has_value());
        if (rules.globalOtherwise) appendDestination(output, *rules.globalOtherwise);
        return output;
    }
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
    std::string output = plan.formatVersion == 1 ? "nookbit-plan-contents-v1" : "nookbit-plan-contents-v2";
    if (plan.formatVersion != 1) appendNumber(output, plan.formatVersion);
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
        if (plan.formatVersion != 1) {
            appendNumber(output, step.movement.has_value());
            if (step.movement) {
                appendNumber(output, step.movement->strategy);
                // 양쪽 볼륨의 모든 승인 필드를 순서대로 추가하는 함수.
                const auto appendVolume = [&](const PlanVolume& volume) {
                    append(output, volume.serial);
                    append(output, volume.guidPath);
                    append(output, volume.filesystem);
                };
                appendVolume(step.movement->sourceVolume);
                appendVolume(step.movement->destinationVolume);
                appendNumber(output, step.movement->preservation);
            }
        }
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
    const std::string prefix = plan.formatVersion == 1 ? "nookbit-approval-plan-v1:" : "nookbit-approval-plan-v2:";
    return prefix + plan.id + ":" + plan.settingsRevision +
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
