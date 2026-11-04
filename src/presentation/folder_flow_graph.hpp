#pragma once

#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>
#include <QHash>
#include <QPointF>
#include <QRegularExpression>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

namespace nookbit {
/// 기본 분류 그래프와 이름 있는 폴더 입력 흐름을 분리한 편집 값.
struct FolderFlowGraph {
    QVariantList blocks;
    QVariantList edges;
    std::vector<RuleSet::FolderFlow> flows;
};

// 폴더 흐름의 고정된 입력·순회·기본 규칙 호출 연결을 검사하는 함수.
inline Result<FolderFlowGraph> splitFolderFlowGraph(const QVariantList& blocks, const QVariantList& edges) {
    FolderFlowGraph result;
    QHash<int, QVariantMap> byId;
    QHash<QString, QVector<int>> groups;
    const QRegularExpression identifier(QStringLiteral("^[a-zA-Z0-9._-]+$"));
    // 잘못된 그룹을 UI 대신 저장·불러오기 경계에서 함께 거부하는 함수.
    const auto invalid = [](const QString& message) {
        return Result<FolderFlowGraph>::failure({"rules.folder_flow_graph_invalid", message.toStdString()});
    };
    for (const auto& value : blocks) {
        const auto block = value.toMap();
        const int id = block.value("id").toInt();
        if (id <= 0 || byId.contains(id)) return invalid(QStringLiteral("노드 번호가 없거나 중복돼요."));
        byId.insert(id, block);
        const auto flowId = block.value("flowId").toString();
        const auto type = block.value("type").toString();
        const bool flowType = type == "flowStart" || type == "folderScan" || type == "applyDefault";
        if (flowId.isEmpty()) {
            if (flowType) return invalid(QStringLiteral("폴더 처리 노드가 속할 흐름이 없어요."));
            result.blocks.append(value);
        } else {
            if (!identifier.match(flowId).hasMatch() || !flowType)
                return invalid(QStringLiteral("폴더 흐름에는 폴더 입력·내부 파일 순회·기본 정리 적용 노드를 사용해 주세요."));
            groups[flowId].append(id);
        }
    }
    if (groups.size() > 24) return invalid(QStringLiteral("폴더 처리 흐름은 24개까지 만들 수 있어요."));
    QHash<int, QVector<int>> outgoing;
    QHash<int, int> incoming;
    for (const auto& value : edges) {
        const auto edge = value.toMap();
        const int from = edge.value("from").toInt();
        const int to = edge.value("to").toInt();
        if (!byId.contains(from) || !byId.contains(to)) return invalid(QStringLiteral("없는 노드로 연결된 선이 있어요."));
        const auto owner = byId.value(from).value("flowId").toString();
        if (owner != byId.value(to).value("flowId").toString())
            return invalid(QStringLiteral("다른 흐름으로 직접 연결할 수 없어요. '기본 정리 적용' 노드를 사용해 주세요."));
        if (owner.isEmpty()) result.edges.append(value);
        else {
            outgoing[from].append(to);
            ++incoming[to];
        }
    }
    QSet<QString> names;
    // 화면의 시작점 순서대로 흐름을 저장하여 선택 메뉴 순서를 보존한다.
    for (const auto& value : blocks) {
        const auto root = value.toMap();
        if (root.value("type").toString() != "flowStart") continue;
        const auto flowId = root.value("flowId").toString();
        const auto name = root.value("value").toString().trimmed();
        if (name.isEmpty() || name.size() > 60 || name.contains('\n') || names.contains(name.toCaseFolded()))
            return invalid(QStringLiteral("폴더 처리 이름은 서로 다르게, 1~60자로 입력해 주세요."));
        names.insert(name.toCaseFolded());
        const auto members = groups.take(flowId);
        const int rootId = root.value("id").toInt();
        if (members.size() != 3 || incoming.value(rootId) || outgoing.value(rootId).size() != 1)
            return invalid(QStringLiteral("폴더 흐름은 폴더 입력 → 내부 파일 순회 → 기본 정리 적용 순서로 연결해 주세요."));
        const int scan = outgoing.value(rootId).front();
        if (byId.value(scan).value("type").toString() != "folderScan" || incoming.value(scan) != 1 || outgoing.value(scan).size() != 1)
            return invalid(QStringLiteral("폴더 입력 다음에는 내부 파일 순회를 연결해 주세요."));
        const int apply = outgoing.value(scan).front();
        if (byId.value(apply).value("type").toString() != "applyDefault" || incoming.value(apply) != 1 || !outgoing.value(apply).isEmpty())
            return invalid(QStringLiteral("순회한 각 파일에는 기본 정리를 적용해 주세요."));
        result.flows.push_back({flowId.toStdString(), name.toStdString()});
    }
    if (!groups.isEmpty()) return invalid(QStringLiteral("폴더 흐름에 시작점이 없거나 중복돼요."));
    return Result<FolderFlowGraph>::success(std::move(result));
}

// 저장된 폴더 흐름을 이동 가능한 세 노드와 연결선으로 복원하는 함수.
inline void appendFolderFlowGraph(const std::vector<RuleSet::FolderFlow>& flows,
    const QHash<QString, QPointF>& positions, QVariantList& blocks, QVariantList& edges, int& nextId, double top) {
    for (const auto& flow : flows) {
        const auto flowId = QString::fromStdString(flow.id);
        const int start = nextId;
        int column = 0;
        for (const auto* typeName : {"flowStart", "folderScan", "applyDefault"}) {
            const auto type = QString::fromLatin1(typeName);
            const auto key = "flow:" + flowId + ":" + type;
            const auto point = positions.value(key, QPointF(70 + column * 310, top));
            blocks.append(QVariantMap{{"id", nextId++}, {"type", type}, {"flowId", flowId},
                {"value", type == "flowStart" ? QString::fromStdString(flow.name) : QString()},
                {"x", point.x()}, {"y", point.y()}});
            ++column;
        }
        edges.append(QVariantMap{{"from", start}, {"to", start + 1}});
        edges.append(QVariantMap{{"from", start + 1}, {"to", start + 2}});
        top += 300;
    }
}
}
