#include "modules/rules/rule_diagnostics.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace nookbit {
namespace {

constexpr int fileType = 1;
constexpr int folderType = 2;
constexpr std::size_t maxAlternatives = 16;
constexpr auto maximum = std::numeric_limits<std::uintmax_t>::max();

struct Range {
    std::uintmax_t minimum{0};
    std::uintmax_t maximumValue{maximum};
};

struct Facts {
    int types{fileType | folderType};
    Range size;
    Range age;
    std::optional<std::set<std::string>> extensions;
    std::set<std::string> excludedExtensions;
};

using Alternatives = std::vector<Facts>;

// 제한을 넘기면 가능 범위는 넓히고 확실히 일치하는 범위는 비우는 함수.
Alternatives unknown(bool guaranteed) {
    return guaranteed ? Alternatives{} : Alternatives{Facts{}};
}

// 두 조건의 공통 범위를 구하는 함수.
std::optional<Facts> intersect(const Facts& left, const Facts& right) {
    Facts result = left;
    result.types &= right.types;
    result.size = {std::max(left.size.minimum, right.size.minimum), std::min(left.size.maximumValue, right.size.maximumValue)};
    result.age = {std::max(left.age.minimum, right.age.minimum), std::min(left.age.maximumValue, right.age.maximumValue)};
    if (result.types == 0 || result.size.minimum > result.size.maximumValue || result.age.minimum > result.age.maximumValue)
        return std::nullopt;
    if (right.extensions) {
        if (!left.extensions) result.extensions = right.extensions;
        else {
            result.extensions.emplace();
            std::set_intersection(left.extensions->begin(), left.extensions->end(),
                right.extensions->begin(), right.extensions->end(),
                std::inserter(*result.extensions, result.extensions->end()));
        }
    }
    result.excludedExtensions.insert(right.excludedExtensions.begin(), right.excludedExtensions.end());
    if (result.extensions) {
        for (const auto& excluded : result.excludedExtensions) result.extensions->erase(excluded);
        if (result.extensions->empty()) return std::nullopt;
        result.excludedExtensions.clear();
    }
    return result;
}

// 각 가능한 범위를 AND로 결합하는 함수.
Alternatives combine(const Alternatives& left, const Alternatives& right, bool guaranteed) {
    Alternatives result;
    for (const auto& a : left) for (const auto& b : right) {
        if (auto intersection = intersect(a, b)) result.push_back(std::move(*intersection));
        if (result.size() > maxAlternatives) return unknown(guaranteed);
    }
    return result;
}

// 지원하는 원자 조건을 만족하지 않는 대상 범위를 구하는 함수.
Alternatives complement(const Facts& facts) {
    Alternatives result;
    Facts otherTypes;
    otherTypes.types &= ~facts.types;
    if (otherTypes.types) result.push_back(otherTypes);

    // 수치 범위 바깥의 양쪽 구간을 같은 대상 종류에 추가하는 함수.
    const auto appendOutside = [&](Range Facts::* field) {
        const auto& range = facts.*field;
        if (range.minimum > 0) {
            Facts lower;
            lower.types = facts.types;
            (lower.*field).maximumValue = range.minimum - 1;
            result.push_back(std::move(lower));
        }
        if (range.maximumValue < maximum) {
            Facts upper;
            upper.types = facts.types;
            (upper.*field).minimum = range.maximumValue + 1;
            result.push_back(std::move(upper));
        }
    };
    appendOutside(&Facts::size);
    appendOutside(&Facts::age);
    if (facts.extensions) {
        Facts otherExtensions;
        otherExtensions.types = facts.types;
        otherExtensions.excludedExtensions = *facts.extensions;
        result.push_back(std::move(otherExtensions));
    }
    return result;
}

// 지원하는 조건을 범위로 바꾸고, 부정은 AND·OR의 반대 연산으로 전파하는 함수.
Alternatives project(const AtomicRuleCondition& condition, bool guaranteed, bool negated = false) {
    if (const auto* expression = std::get_if<LogicalPredicatePtr>(&condition)) {
        if (!*expression) return unknown(guaranteed);
        const auto& group = **expression;
        if (group.operation == LogicalOperator::Not)
            return group.operands.size() == 1 ? project(group.operands.front(), guaranteed, !negated)
                : unknown(guaranteed);
        const bool all = (group.operation == LogicalOperator::All) != negated;
        Alternatives result = all ? Alternatives{Facts{}} : Alternatives{};
        for (const auto& operand : group.operands) {
            auto child = project(operand, guaranteed, negated);
            if (all) {
                result = combine(result, child, guaranteed);
                if (result.empty()) break;
            } else {
                result.insert(result.end(), child.begin(), child.end());
                if (result.size() > maxAlternatives) return unknown(guaranteed);
            }
        }
        return result;
    }
    Facts facts;
    if (const auto* type = std::get_if<ItemTypePredicate>(&condition)) {
        facts.types = type->value == ItemType::Folder ? folderType : fileType;
    } else if (const auto* extension = std::get_if<ExtensionPredicate>(&condition)) {
        facts.types = fileType;
        facts.extensions.emplace();
        for (auto value : extension->values) {
            if (!value.empty() && value.front() == '.') value.erase(0, 1);
            for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            facts.extensions->insert(std::move(value));
        }
        if (facts.extensions->empty()) return negated ? Alternatives{Facts{}} : Alternatives{};
    } else if (const auto* size = std::get_if<SizePredicate>(&condition)) {
        facts.types = fileType;
        switch (size->comparison) {
        case SizeComparison::GreaterThan:
            if (size->bytes == maximum) return negated ? Alternatives{Facts{}} : Alternatives{};
            facts.size.minimum = size->bytes + 1;
            break;
        case SizeComparison::AtLeast: facts.size.minimum = size->bytes; break;
        case SizeComparison::LessThan:
            if (size->bytes == 0) return negated ? Alternatives{Facts{}} : Alternatives{};
            facts.size.maximumValue = size->bytes - 1;
            break;
        case SizeComparison::AtMost: facts.size.maximumValue = size->bytes; break;
        }
    } else if (const auto* age = std::get_if<ModifiedAgePredicate>(&condition)) {
        if (age->comparison == AgeComparison::LessThan) {
            if (age->days <= 0) return negated ? Alternatives{Facts{}} : Alternatives{};
            facts.age.maximumValue = static_cast<std::uintmax_t>(age->days - 1);
        } else if (age->days > 0) {
            facts.age.minimum = static_cast<std::uintmax_t>(age->days);
        }
    } else {
        return unknown(guaranteed);
    }
    return negated ? complement(facts) : Alternatives{std::move(facts)};
}

// 앞 조건이 뒤 조건의 가능한 범위를 모두 포함하는지 확인하는 함수.
bool covers(const Facts& earlier, const Facts& later) {
    const bool respectsExclusions = earlier.excludedExtensions.empty()
        || (later.extensions
            ? std::none_of(later.extensions->begin(), later.extensions->end(),
                [&](const std::string& value) { return earlier.excludedExtensions.contains(value); })
            : std::includes(later.excludedExtensions.begin(), later.excludedExtensions.end(),
                earlier.excludedExtensions.begin(), earlier.excludedExtensions.end()));
    return (earlier.types & later.types) == later.types
        && earlier.size.minimum <= later.size.minimum && earlier.size.maximumValue >= later.size.maximumValue
        && earlier.age.minimum <= later.age.minimum && earlier.age.maximumValue >= later.age.maximumValue
        && respectsExclusions
        && (!earlier.extensions || (later.extensions && std::includes(earlier.extensions->begin(),
            earlier.extensions->end(), later.extensions->begin(), later.extensions->end())));
}

struct EarlierBranch {
    std::string id;
    Alternatives guaranteed;
};

// 한 가지의 도달 범위를 계산하고 모순 또는 선행 가지의 가림을 기록하는 함수.
Alternatives inspectBranch(const std::string& flowId, const std::string& id,
                           const std::optional<AtomicRuleCondition>& condition, bool otherwise,
                           const Alternatives& incoming, std::vector<EarlierBranch>& earlier,
                           std::vector<RuleWarning>& warnings) {
    auto possible = condition ? combine(incoming, project(*condition, false), false) : incoming;
    if (possible.empty()) {
        warnings.push_back({flowId, id, {}});
        return {};
    }
    for (const auto& branch : earlier) {
        const bool shadowed = std::all_of(possible.begin(), possible.end(), [&](const Facts& target) {
            return std::any_of(branch.guaranteed.begin(), branch.guaranteed.end(),
                [&](const Facts& prior) { return covers(prior, target); });
        });
        if (shadowed) {
            warnings.push_back({flowId, id, branch.id});
            return {};
        }
    }
    if (!otherwise) {
        auto guaranteed = condition ? project(*condition, true) : Alternatives{Facts{}};
        if (!guaranteed.empty()) earlier.push_back({id, std::move(guaranteed)});
    }
    return possible;
}

// 기본 분류에서 상위 조건과 같은 부모의 검사 순서를 따라가는 함수.
void inspectTree(const std::vector<RuleNode>& nodes, const Alternatives& incoming,
                 std::vector<RuleWarning>& warnings) {
    std::vector<EarlierBranch> earlier;
    for (const auto& node : nodes) {
        if (!node.enabled) continue;
        const auto possible = inspectBranch({}, node.id, node.atomicCondition, node.otherwise, incoming, earlier, warnings);
        if (!possible.empty()) inspectTree(node.children, possible, warnings);
    }
}

// 이름 있는 흐름의 분기를 검사하고 순회 뒤에는 새 파일의 범위를 사용하는 함수.
void inspectFlow(const RuleSet::FolderFlow& flow, const std::map<std::string, const ProcessingNode*>& nodes,
                 const ProcessingNode& parent, const Alternatives& incoming, std::vector<RuleWarning>& warnings) {
    std::vector<EarlierBranch> earlier;
    for (const auto& id : parent.next) {
        const auto& node = *nodes.at(id);
        auto possible = inspectBranch(flow.id, id, node.condition, node.type == "otherwise", incoming, earlier, warnings);
        if (possible.empty()) continue;
        if (node.type == "traverse") {
            Facts files;
            files.types = fileType;
            possible = {files};
        }
        inspectFlow(flow, nodes, node, possible, warnings);
    }
}
}

std::vector<RuleWarning> inspectRules(const RuleSet& rules) {
    std::vector<RuleWarning> warnings;
    inspectTree(rules.nodes, {Facts{}}, warnings);
    for (const auto& flow : rules.folderFlows) {
        if (flow.nodes.empty()) continue;
        std::map<std::string, const ProcessingNode*> nodes;
        for (const auto& node : flow.nodes) nodes.emplace(node.id, &node);
        Facts input;
        if (flow.folderInput) input.types = folderType;
        inspectFlow(flow, nodes, *nodes.at(flow.entry), {input}, warnings);
    }
    return warnings;
}
}
