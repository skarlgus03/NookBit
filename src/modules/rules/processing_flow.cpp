#include "modules/rules/processing_flow.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <regex>

namespace nookbit {
// 이전 프리셋을 기본 입력·순회·호출 그래프로 변환하는 함수.
RuleSet::FolderFlow expandedFlow(const RuleSet::FolderFlow& value) {
    if (!value.nodes.empty()) return value;
    auto flow = value;
    flow.entry = "input";
    flow.nodes = {{"input", "input", {"scan"}}, {"scan", "traverse", {"apply"}}, {"apply", "call"}};
    return flow;
}

// 유효한 식별자와 DAG·호출 관계를 확인하고 오류에 흐름 위치를 붙이는 함수.
Result<void> validateProcessingFlows(const RuleSet& rules) {
    const std::regex id("^[A-Za-z0-9._-]+$");
    const auto invalid = [](const std::string& message) {
        return Result<void>::failure({"rules.flow_invalid", message});
    };
    if (rules.folderFlows.size() > 24) return invalid("$.folderFlows: at most 24 flows are supported.");
    std::map<std::string, RuleSet::FolderFlow> flows;
    std::size_t total = 0;
    for (const auto& value : rules.folderFlows) {
        auto flow = expandedFlow(value);
        if (!std::regex_match(flow.id, id) || flow.name.empty() || !flows.emplace(flow.id, flow).second)
            return invalid("$.folderFlows: missing or duplicate flow ID/name.");
        total += flow.nodes.size();
    }
    if (total > 1000) return invalid("$.folderFlows: total node limit is 1000.");
    std::map<std::string, std::set<std::string>> calls;
    for (const auto& [flowId, flow] : flows) {
        const auto fail = [&](const std::string& message) { return invalid("Flow " + flowId + ": " + message); };
        std::map<std::string, const ProcessingNode*> nodes;
        for (const auto& node : flow.nodes)
            if (!std::regex_match(node.id, id) || !nodes.emplace(node.id, &node).second) return fail("duplicate or invalid node ID.");
        if (!nodes.contains(flow.entry) || nodes.at(flow.entry)->type != "input") return fail("entry must name an input node.");
        std::set<std::string> visiting, visited;
        std::map<std::string, int> parents;
        std::function<Result<void>(const std::string&, int, int)> check;
        // 깊이와 순회 이후 파일 전용 입력을 전달하며 각 노드를 검사하는 함수.
        check = [&](const std::string& key, int depth, int inputTypes) -> Result<void> {
            if (depth > 32 || visiting.contains(key)) return fail("cycle or excessive graph depth at " + key);
            if (visited.contains(key)) return fail("nodes may have only one parent: " + key);
            if (!nodes.contains(key)) return fail("unknown node: " + key);
            const auto& node = *nodes.at(key);
            const bool conditional = node.type == "condition";
            const bool terminal = node.type == "destination" || node.type == "exclude" || node.type == "call";
            if (node.type != "input" && node.type != "traverse" && !conditional && node.type != "otherwise" && !terminal)
                return fail("unsupported node type: " + node.type);
            if ((node.type == "input" && key != flow.entry) || conditional != node.condition.has_value() ||
                (node.type == "destination") != node.destination.has_value() ||
                (terminal ? !node.next.empty() : node.next.empty())) return fail("invalid fields or missing successor at " + key);
            if (node.type != "call" && !node.targetFlow.empty()) return fail("only call nodes can choose another flow.");
            if (node.type == "traverse" && (inputTypes & 1)) return fail("traversal requires a folder input or folder-type guard at " + key);
            if (node.type == "call" && !node.targetFlow.empty()) {
                if (!flows.contains(node.targetFlow)) return fail("unknown called flow: " + node.targetFlow);
                if ((inputTypes & 1) && flows.at(node.targetFlow).folderInput) return fail("file output cannot call a folder-only flow.");
                calls[flowId].insert(node.targetFlow);
            }
            visiting.insert(key);
            int outputTypes = node.type == "traverse" ? 1 : inputTypes;
            if (node.condition) {
                if (const auto* type = std::get_if<ItemTypePredicate>(&*node.condition)) outputTypes &= type->value == ItemType::Folder ? 2 : 1;
                else if (std::holds_alternative<ExtensionPredicate>(*node.condition) || std::holds_alternative<SizePredicate>(*node.condition)) outputTypes &= 1;
            }
            bool otherwiseSeen = false;
            for (std::size_t index = 0; index < node.next.size(); ++index) {
                const auto& child = node.next[index];
                if (!nodes.contains(child)) return fail("unknown successor: " + child);
                const auto& childType = nodes.at(child)->type;
                if (node.next.size() > 1 && childType != "condition" && childType != "otherwise")
                    return fail("branch siblings must be conditions or otherwise.");
                if (childType == "otherwise") {
                    if (otherwiseSeen || index + 1 != node.next.size()) return fail("otherwise must be unique and last.");
                    otherwiseSeen = true;
                }
                if (++parents[child] > 1 || child == flow.entry) return fail("cycle or multiple input parents.");
                auto checked = check(child, depth + 1, outputTypes);
                if (!checked) return checked;
            }
            visiting.erase(key); visited.insert(key);
            return Result<void>::success();
        };
        const auto checked = check(flow.entry, 0, flow.folderInput ? 2 : 3);
        if (!checked) return checked;
        if (visited.size() != nodes.size()) return fail("unreachable nodes are not allowed.");
    }
    std::set<std::string> visiting, complete;
    std::function<bool(const std::string&)> visit;
    // 노드의 순환과 별개로 흐름 사이의 재귀 호출을 검사하는 함수.
    visit = [&](const std::string& key) {
        if (visiting.contains(key)) return false;
        if (complete.contains(key)) return true;
        visiting.insert(key);
        for (const auto& next : calls[key]) if (!visit(next)) return false;
        visiting.erase(key); complete.insert(key); return true;
    };
    for (const auto& [key, flow] : flows) if (!visit(key)) return invalid("$.folderFlows: recursive flow calls are not allowed.");
    return Result<void>::success();
}

// 선택했던 개별 원본에만 승인된 흐름 재개 위치를 붙이는 함수.
Result<void> restoreProcessingSources(ScanSnapshot& snapshot, const OperationPlan& plan) {
    for (const auto& context : plan.processingSources) {
        const auto found = std::find_if(snapshot.files.begin(), snapshot.files.end(), [&](const FileFact& file) { return file.absolutePath == context.source; });
        if (found == snapshot.files.end() || !found->processingFlowId.empty())
            return Result<void>::failure({"flow.context_invalid", "Flow input source is missing or duplicated."});
        found->processingFlowId = context.flowId;
        found->processingAfterNode = context.afterNode;
    }
    return Result<void>::success();
}
}
