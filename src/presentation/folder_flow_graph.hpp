#pragma once
#include <nookbit/domain/model.hpp>
#include "modules/rules/processing_flow.hpp"
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>

namespace nookbit {
struct FolderFlowGraph {
    QVariantList blocks;
    QVariantList edges;
    std::vector<RuleSet::FolderFlow> flows;
};
using FlowConditionParser = std::function<Result<AtomicRuleCondition>(const QVariantMap&)>;
using FlowConditionFormatter = std::function<QVariantMap(const ProcessingNode&, int)>;

// 논리 블록과 중첩된 조건 카드에 필요한 자동 배치 줄 수를 구하는 함수.
inline int logicalGraphRows(const QVariantMap& block) {
    int rows = 1;
    for (const auto& operand : block.value("operands").toList()) {
        rows += logicalGraphRows(operand.toMap());
    }
    return rows;
}

// 각 영역의 실제 노드와 연결을 처리 데이터로 변환하는 함수.
inline Result<FolderFlowGraph> splitFolderFlowGraph(const QVariantList& blocks, const QVariantList& edges,
                                                    const FlowConditionParser& conditionParser = {}) {
    FolderFlowGraph result;
    QHash<int, QVariantMap> byId;
    QHash<QString, QVector<int>> groups;
    QHash<int, QString> nodeIds;
    const auto invalid = [](const QString& message) { return Result<FolderFlowGraph>::failure({"rules.folder_flow_graph_invalid", message.toStdString()}); };
    for (const auto& value : blocks) {
        const auto block = value.toMap();
        const int id = block.value("id").toInt();
        if (id <= 0 || byId.contains(id)) return invalid(QStringLiteral("노드 번호가 없거나 중복돼요."));
        byId.insert(id, block);
        const auto flowId = block.value("flowId").toString();
        const auto type = block.value("type").toString();
        if (flowId.isEmpty()) {
            if (type == "flowStart" || type == "folderScan" || type == "callFlow" || type == "applyDefault")
                return invalid(QStringLiteral("처리 노드는 이름 있는 흐름 안에 추가해 주세요."));
            result.blocks.append(value);
        } else {
            groups[flowId].append(id);
            nodeIds[id] = block.value("flowNodeId", QStringLiteral("node-%1").arg(id)).toString();
        }
    }
    QHash<int, QVector<int>> outgoing;
    for (const auto& value : edges) {
        const auto edge = value.toMap();
        const int from = edge.value("from").toInt(), to = edge.value("to").toInt();
        if (!byId.contains(from) || !byId.contains(to)) return invalid(QStringLiteral("없는 노드로 연결된 선이 있어요."));
        const auto owner = byId.value(from).value("flowId").toString();
        if (owner != byId.value(to).value("flowId").toString())
            return invalid(QStringLiteral("영역 사이에는 선 대신 '분류 흐름 호출' 노드를 사용해 주세요."));
        if (owner.isEmpty()) result.edges.append(value);
        else outgoing[from].append(to);
    }
    QSet<QString> names;
    for (const auto& value : blocks) {
        const auto root = value.toMap();
        if (root.value("type") != "flowStart") continue;
        const auto flowId = root.value("flowId").toString();
        const auto name = root.value("value").toString().trimmed();
        if (name.isEmpty() || name.size() > 60 || name.contains('\n') || names.contains(name.toCaseFolded()) || !groups.contains(flowId))
            return invalid(QStringLiteral("흐름 이름은 서로 다르게, 1~60자로 입력하고 입력 노드는 하나만 두세요."));
        names.insert(name.toCaseFolded());
        RuleSet::FolderFlow flow{flowId.toStdString(), name.toStdString()};
        flow.entry = nodeIds.value(root.value("id").toInt()).toStdString();
        if (root.value("flowInput", "folder") != "folder" && root.value("flowInput", "folder") != "item")
            return invalid(QStringLiteral("흐름 입력에서 폴더 또는 파일·폴더를 선택해 주세요."));
        flow.folderInput = root.value("flowInput", "folder").toString() == "folder";
        for (const auto id : groups.take(flowId)) {
            const auto block = byId.value(id);
            const auto type = block.value("type").toString();
            ProcessingNode node; node.id = nodeIds.value(id).toStdString();
            for (const auto child : outgoing.value(id)) node.next.push_back(nodeIds.value(child).toStdString());
            if (type == "flowStart") node.type = "input";
            else if (type == "folderScan") {
                node.type = "traverse";
                node.recursive = block.value("recursive", true).toBool();
                if (block.contains("maxDepth")) {
                    const auto value = block.value("maxDepth");
                    const auto kind = value.metaType().id();
                    const double depth = value.toDouble();
                    const bool numeric = kind == QMetaType::Int || kind == QMetaType::UInt ||
                        kind == QMetaType::LongLong || kind == QMetaType::ULongLong ||
                        kind == QMetaType::Double || kind == QMetaType::Float;
                    if (!numeric ||
                        !std::isfinite(depth) || depth < 1 || depth > std::numeric_limits<int>::max() ||
                        std::floor(depth) != depth || !node.recursive)
                        return invalid(QStringLiteral("블록 %1: 하위 폴더 깊이는 1 이상의 정수로 입력해 주세요.").arg(id));
                    node.maxDepth = static_cast<int>(depth);
                }
            }
            else if (type == "callFlow" || type == "applyDefault") { node.type = "call"; node.targetFlow = block.value("targetFlow").toString().toStdString(); }
            else if (type == "otherwise" || type == "exclude") node.type = type.toStdString();
            else if (type == "destination") {
                node.type = "destination";
                const auto text = block.value("value").toString().trimmed();
                const std::filesystem::path path(text.toStdWString());
                const bool absolute = !block.value("homeRelative", true).toBool();
                auto tokens = text; tokens.replace("{yyyy}", ""); tokens.replace("{MM}", "");
                if (text.isEmpty() || (absolute ? !path.is_absolute() : path.has_root_path()) || tokens.contains('{') || tokens.contains('}'))
                    return invalid(QStringLiteral("목적지 경로와 기준 폴더를 확인해 주세요."));
                for (const auto& part : path) if (part == "..") return invalid(QStringLiteral("목적지에 상위 경로를 사용할 수 없어요."));
                std::optional<CollisionPolicy> policy;
                const auto collision = block.value("collisionPolicy", "inherit").toString();
                if (collision == "rename") policy = CollisionPolicy::Rename;
                else if (collision == "skip") policy = CollisionPolicy::Skip;
                else if (collision == "abort") policy = CollisionPolicy::Abort;
                else if (collision != "inherit") return invalid(QStringLiteral("충돌 처리 설정이 올바르지 않아요."));
                node.destination = RuleDestination{path, policy, absolute};
            } else {
                if (!conditionParser) return invalid(QStringLiteral("알 수 없는 처리 노드예요."));
                const auto condition = conditionParser(block);
                if (!condition) return Result<FolderFlowGraph>::failure(condition.error());
                node.type = "condition"; node.condition = condition.value();
            }
            flow.nodes.push_back(std::move(node));
        }
        result.flows.push_back(std::move(flow));
    }
    if (!groups.isEmpty()) return invalid(QStringLiteral("흐름에 입력 노드가 없어요."));
    RuleSet check; check.folderFlows = result.flows;
    const auto valid = validateProcessingFlows(check);
    if (!valid) return Result<FolderFlowGraph>::failure(valid.error());
    return Result<FolderFlowGraph>::success(std::move(result));
}

// 코어 그래프를 조건·순회·호출 노드와 실제 연결선으로 복원하는 함수.
inline void appendFolderFlowGraph(const std::vector<RuleSet::FolderFlow>& flows,
    const QHash<QString, QPointF>& positions, QVariantList& blocks, QVariantList& edges, int& nextId, double top,
    const FlowConditionFormatter& formatCondition = {}) {
    for (const auto& value : flows) {
        const auto flow = expandedFlow(value);
        const auto flowId = QString::fromStdString(flow.id);
        QHash<QString, int> ids;
        for (const auto& node : flow.nodes) ids.insert(QString::fromStdString(node.id), nextId++);
        int row = 0;
        int logicalGroups = 0;
        QHash<QString, QPointF> defaults;
        std::function<double(const std::string&, int)> arrange;
        // 자식의 중앙에 부모를 놓고 분기를 세로로 배치하는 함수.
        arrange = [&](const std::string& key, int depth) {
            const auto node = std::find_if(flow.nodes.begin(), flow.nodes.end(), [&](const auto& item) { return item.id == key; });
            if (node == flow.nodes.end()) return top;
            const int rows = node->type == "condition" && formatCondition
                ? logicalGraphRows(formatCondition(*node, ids.value(QString::fromStdString(key)))) : 1;
            const int groupsBeforeChildren = logicalGroups;
            double y = top;
            if (!node->next.empty()) {
                double first = 0, last = 0;
                for (std::size_t i = 0; i < node->next.size(); ++i) { last = arrange(node->next[i], depth + 1); if (i == 0) first = last; }
                y = (first + last) / 2;
            }
            if (node->next.empty() || rows > 1 || logicalGroups != groupsBeforeChildren) {
                y = top + row * 170;
                row += rows;
            }
            if (rows > 1) ++logicalGroups;
            defaults.insert(QString::fromStdString(key), QPointF(70 + depth * 310, y));
            return y;
        };
        arrange(flow.entry, 0);
        for (const auto& node : flow.nodes) {
            const auto key = QString::fromStdString(node.id);
            const auto id = ids.value(key);
            QVariantMap block;
            if (node.type == "condition" && formatCondition) block = formatCondition(node, id);
            else {
                const auto type = node.type == "input" ? "flowStart" : node.type == "traverse" ? "folderScan" : node.type == "call" ? "callFlow" : node.type.c_str();
                block = {{"id", id}, {"type", QString::fromUtf8(type)}, {"value", node.type == "input" ? QString::fromStdString(flow.name) : QString()}};
            }
            const auto type = block.value("type").toString();
            const auto fallback = positions.value("flow:" + flowId + ":" + (type == "callFlow" ? "applyDefault" : type), defaults.value(key));
            const auto point = positions.value("flow:" + flowId + ":" + key, fallback);
            block.insert("flowId", flowId); block.insert("flowNodeId", key);
            block.insert("flowInput", flow.folderInput ? "folder" : "item");
            block.insert("x", point.x()); block.insert("y", point.y());
            if (node.type == "call") block.insert("targetFlow", QString::fromStdString(node.targetFlow));
            if (node.type == "traverse") {
                block.insert("recursive", node.recursive);
                if (node.maxDepth) block.insert("maxDepth", *node.maxDepth);
            }
            if (node.destination) {
                block.insert("value", QString::fromStdWString(node.destination->relativePath.wstring()));
                block.insert("homeRelative", !node.destination->absolute);
                block.insert("collisionPolicy", !node.destination->collisionPolicy ? "inherit" : *node.destination->collisionPolicy == CollisionPolicy::Rename ? "rename" : *node.destination->collisionPolicy == CollisionPolicy::Skip ? "skip" : "abort");
            }
            blocks.append(block);
            for (const auto& child : node.next) edges.append(QVariantMap{{"from", id}, {"to", ids.value(QString::fromStdString(child))}});
        }
        top += std::max(1, row) * 170 + 200;
    }
}
}
