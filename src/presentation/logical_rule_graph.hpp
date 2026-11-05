#pragma once
#include <nookbit/domain/result.hpp>
#include "modules/rules/logical_predicate.hpp"
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace nookbit {
struct LogicalRuleGraph { QVariantList blocks; QVariantList edges; };

// 조건 계산 연결을 중첩된 값으로 묶고 실행 연결만 기존 트리 검사기에 전달하는 함수.
inline Result<LogicalRuleGraph> prepareLogicalGraph(const QVariantList& blocks, const QVariantList& edges) {
    // 오류가 발생한 노드를 포함해 검증 실패를 반환하는 함수.
    const auto invalid = [](int id, const QString& reason) {
        return Result<LogicalRuleGraph>::failure({"rules.logical_graph_invalid",
            QStringLiteral("블록 %1: %2").arg(id).arg(reason).toStdString()});
    };
    if (blocks.size() > 1000) return invalid(0, QStringLiteral("노드는 1000개까지 사용할 수 있어요."));
    QHash<int, QVariantMap> nodes;
    for (const auto& value : blocks) {
        const auto block = value.toMap(); const int id = block.value("id").toInt();
        if (id <= 0 || nodes.contains(id)) return invalid(id, QStringLiteral("노드 번호가 없거나 중복돼요."));
        nodes.insert(id, block);
    }
    QHash<int, QList<int>> operands;
    QSet<int> consumed, executionTargets, executionSources;
    LogicalRuleGraph result;
    const QStringList conditions{"extension", "name", "size", "age", "itemType", "sourceFolder"};
    for (const auto& value : edges) {
        const auto edge = value.toMap(); const int from = edge.value("from").toInt(), to = edge.value("to").toInt();
        if (!nodes.contains(from) || !nodes.contains(to)) return invalid(from, QStringLiteral("존재하지 않는 노드로 이어진 선이에요."));
        const auto kind = edge.value("kind").toString();
        if (kind.isEmpty()) {
            result.edges.append(value); executionTargets.insert(to); executionSources.insert(from); continue;
        }
        const auto sourceType = nodes[from].value("type").toString();
        if (kind != "predicate" || (!conditions.contains(sourceType) && !logicalOperatorFor(sourceType.toStdString()))
            || !logicalOperatorFor(nodes[to].value("type").toString().toStdString()))
            return invalid(to, QStringLiteral("조건 계산 선은 조건에서 AND·OR·NOT으로만 연결할 수 있어요."));
        if (nodes[from].value("flowId").toString() != nodes[to].value("flowId").toString())
            return invalid(to, QStringLiteral("조건 묶음은 같은 흐름 안에서 연결해 주세요."));
        if (consumed.contains(from)) return invalid(from, QStringLiteral("조건 출력은 묶음 하나에만 연결할 수 있어요."));
        if (!nodes[from].value("enabled", true).toBool()) return invalid(from, QStringLiteral("묶음 안의 조건은 활성화해 주세요."));
        consumed.insert(from); operands[to].append(from);
    }
    for (const int id : consumed) if (executionTargets.contains(id) || executionSources.contains(id))
        return invalid(id, QStringLiteral("묶음에 사용한 조건에는 실행 선을 함께 연결할 수 없어요."));
    QSet<int> visiting;
    std::function<Result<QVariantMap>(int, int)> build;
    // 조건 입력만 따라가며 순환·개수·깊이를 검사하는 함수.
    build = [&](int id, int depth) -> Result<QVariantMap> {
        if (depth > 32 || visiting.contains(id))
            return Result<QVariantMap>::failure(invalid(id, QStringLiteral("논리 조건 연결에 순환이 있거나 깊이가 32를 넘었어요.")).error());
        auto block = nodes.value(id);
        const auto operation = logicalOperatorFor(block.value("type").toString().toStdString());
        if (!operation) return Result<QVariantMap>::success(block);
        const auto inputsForBlock = operands.value(id);
        if (!validLogicalArity(*operation, static_cast<std::size_t>(inputsForBlock.size()))) {
            const auto reason = *operation == LogicalOperator::Not ? QStringLiteral("NOT에는 조건을 하나만 연결해 주세요.")
                : QStringLiteral("AND·OR에는 조건을 두 개 이상 연결해 주세요.");
            return Result<QVariantMap>::failure(invalid(id, reason).error());
        }
        visiting.insert(id); QVariantList inputs;
        for (const int input : inputsForBlock) {
            const auto child = build(input, depth + 1); if (!child) return child;
            inputs.append(child.value());
        }
        visiting.remove(id); block["operands"] = inputs;
        return Result<QVariantMap>::success(block);
    };
    // 닫힌 순환도 놓치지 않도록 실행 경로에서 분리된 묶음까지 검사한다.
    for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
        const auto checked = build(it.key(), 0);
        if (!checked) return Result<LogicalRuleGraph>::failure(checked.error());
        if (!consumed.contains(it.key())) result.blocks.append(checked.value());
    }
    return Result<LogicalRuleGraph>::success(result);
}
}
