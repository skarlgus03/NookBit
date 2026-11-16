#pragma once

#include "modules/rules/processing_flow.hpp"

#include <QHash>
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>

namespace nookbit {

inline Result<DocumentOutput> graphDocumentOutput(const QVariantMap& terminal) {
    if (terminal.isEmpty()) return Result<DocumentOutput>::success({});
    if (terminal.value("type") == "exclude") return Result<DocumentOutput>::success({{}, true});
    const auto invalid = [](const char* message) {
        return Result<DocumentOutput>::failure({"rules.document_analysis_invalid", message});
    };
    if (terminal.value("type") != "destination") return invalid("출력은 목적지 또는 정리 제외에 연결해 주세요.");
    const auto text = terminal.value("value").toString().trimmed();
    const std::filesystem::path path(text.toStdWString());
    const bool absolute = !terminal.value("homeRelative", true).toBool();
    auto tokens = text;
    tokens.replace("{yyyy}", "");
    tokens.replace("{MM}", "");
    if (text.isEmpty() || text.contains(QChar(0)) || (absolute ? !path.is_absolute() : path.has_root_path()) ||
        tokens.contains('{') || tokens.contains('}'))
        return invalid("목적지 경로와 기준 폴더를 확인해 주세요.");
    for (const auto& part : path)
        if (part == "..") return invalid("목적지에 상위 경로를 사용할 수 없어요.");
    std::optional<CollisionPolicy> policy;
    const auto collision = terminal.value("collisionPolicy", "inherit").toString();
    if (collision == "rename") policy = CollisionPolicy::Rename;
    else if (collision == "skip") policy = CollisionPolicy::Skip;
    else if (collision == "abort") policy = CollisionPolicy::Abort;
    else if (collision != "inherit") return invalid("충돌 처리 설정이 올바르지 않아요.");
    return Result<DocumentOutput>::success({RuleDestination{path, policy, absolute}});
}

// 화면의 출력 핀을 규칙에 내장된 분류별 종착점으로 변환한다.
inline Result<DocumentAnalysis> graphDocumentAnalysis(const QVariantMap& block,
    QHash<QString, QVariantMap> terminals) {
    DocumentAnalysis analysis;
    bool validSimilarity = false;
    analysis.minimumSimilarity = block.value("minimumSimilarity").toInt(&validSimilarity);
    if (!validSimilarity || block.value("minimumSimilarity").toDouble() != analysis.minimumSimilarity)
        return Result<DocumentAnalysis>::failure({"rules.document_analysis_invalid", "최소 유사도를 확인해 주세요."});
    auto abstain = graphDocumentOutput(terminals.take("abstain"));
    if (!abstain) return Result<DocumentAnalysis>::failure(abstain.error());
    analysis.abstain = std::move(abstain.value());
    const auto categories = block.value("categories").toList();
    for (const auto& value : categories) {
        const auto category = value.toMap();
        const auto id = category.value("id").toString();
        auto output = graphDocumentOutput(terminals.take(id));
        if (!output) return Result<DocumentAnalysis>::failure(output.error());
        analysis.categories.push_back({id.toStdString(), category.value("label").toString().trimmed().toStdString(),
            category.value("keywords").toString().trimmed().toStdString(), std::move(output.value())});
    }
    if (!terminals.isEmpty())
        return Result<DocumentAnalysis>::failure({"rules.document_analysis_invalid", "존재하지 않는 분류 출력에 선이 연결됐어요."});
    const auto valid = validateDocumentAnalysis(analysis);
    if (!valid) return Result<DocumentAnalysis>::failure(valid.error());
    return Result<DocumentAnalysis>::success(std::move(analysis));
}

inline QString documentOutputPositionKey(const QString& stableOwner, const QString& port) {
    return QStringLiteral("document-output:") + stableOwner + ':' + port;
}

inline QString documentOutputPolicy(const std::optional<CollisionPolicy>& policy) {
    if (policy == CollisionPolicy::Rename) return QStringLiteral("rename");
    if (policy == CollisionPolicy::Skip) return QStringLiteral("skip");
    if (policy == CollisionPolicy::Abort) return QStringLiteral("abort");
    return QStringLiteral("inherit");
}

// 노드 설정과 함께 연결했던 출력만 다시 화면에 펼친다.
inline void appendGraphDocumentOutputs(const DocumentAnalysis& analysis, QVariantMap& block,
    QVariantList& blocks, QVariantList& edges, const QHash<QString, QPointF>& positions,
    const QString& stableOwner, int& nextId) {
    block["type"] = QStringLiteral("documentAnalysis");
    block["minimumSimilarity"] = analysis.minimumSimilarity;
    const int owner = block.value("id").toInt();
    const auto flowId = block.value("flowId").toString();
    int row = 0;
    const auto appendOutput = [&](const QString& port, const DocumentOutput& output) {
        const int positionRow = row++;
        if (!output.exclude && !output.destination) return;
        const int id = nextId++;
        const auto position = positions.value(documentOutputPositionKey(stableOwner, port),
            QPointF(block.value("x").toDouble() + 310, block.value("y").toDouble() + 72 + positionRow * 76));
        QVariantMap terminal{{"id", id}, {"type", output.exclude ? "exclude" : "destination"},
            {"x", position.x()}, {"y", position.y()}};
        if (output.destination) {
            terminal["value"] = QString::fromStdWString(output.destination->relativePath.wstring());
            terminal["homeRelative"] = !output.destination->absolute;
            terminal["collisionPolicy"] = documentOutputPolicy(output.destination->collisionPolicy);
        } else {
            terminal["value"] = QStringLiteral("이 파일은 이동하지 않음");
        }
        if (!flowId.isEmpty()) terminal["flowId"] = flowId;
        blocks.append(terminal);
        edges.append(QVariantMap{{"from", owner}, {"to", id}, {"port", port}});
    };
    QVariantList categories;
    for (const auto& category : analysis.categories) {
        const auto id = QString::fromStdString(category.id);
        categories.append(QVariantMap{{"id", id}, {"label", QString::fromStdString(category.label)},
                                     {"keywords", QString::fromStdString(category.keywords)}});
        appendOutput(id, category.output);
    }
    block["categories"] = categories;
    appendOutput(QStringLiteral("abstain"), analysis.abstain);
}

} // namespace nookbit
