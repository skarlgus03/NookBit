#include "presentation/rule_editor_controller.hpp"
#include "application/use_cases/test_rule_use_case.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace nookbit {
namespace {
// 사용자 단위를 정확한 정수 바이트 배수로 변환하고 기존 MiB도 지원하는 함수.
qulonglong sizeFactor(const QString& unit) {
    if (unit == "B") return 1;
    if (unit == "KB") return 1000;
    if (unit == "MB") return 1000000;
    if (unit == "GB") return 1000000000;
    if (unit == "MiB") return 1048576;
    return 0;
}
// 반올림 없이 원래 바이트 값을 보존하는 표시 단위와 정수를 고르는 함수.
std::pair<qulonglong, QString> sizeDisplay(qulonglong bytes) {
    for (const auto* unit : {"GB", "MB", "MiB", "KB"}) {
        const auto factor = sizeFactor(QString::fromLatin1(unit));
        if (bytes > 0 && bytes % factor == 0) return {bytes / factor, QString::fromLatin1(unit)};
    }
    return {bytes, QStringLiteral("B")};
}

// 숫자 경로를 따라 트리의 형제 목록을 찾는 함수.
std::vector<RuleNode>* siblingsAt(RuleSet& rules, const QString& parentPath) {
    auto* siblings = &rules.nodes;
    if (parentPath.isEmpty()) return siblings;
    for (const auto& part : parentPath.split(QLatin1Char('/'))) {
        bool valid = false;
        const int index = part.toInt(&valid);
        if (!valid || index < 0 || static_cast<std::size_t>(index) >= siblings->size())
            return nullptr;
        siblings = &(*siblings)[static_cast<std::size_t>(index)].children;
    }
    return siblings;
}

// 숫자 경로로 초안의 노드 하나를 찾는 함수.
RuleNode* nodeAt(RuleSet& rules, const QString& path) {
    if (path.isEmpty()) return nullptr;
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString parent = slash < 0 ? QString() : path.left(slash);
    auto* siblings = siblingsAt(rules, parent);
    bool valid = false;
    const int index = path.mid(slash + 1).toInt(&valid);
    if (!siblings || !valid || index < 0 ||
        static_cast<std::size_t>(index) >= siblings->size()) return nullptr;
    return &(*siblings)[static_cast<std::size_t>(index)];
}

// 읽기 전용 초안에서 숫자 경로의 노드를 찾는 함수.
const RuleNode* nodeAt(const RuleSet& rules, const QString& path) {
    if (path.isEmpty()) return nullptr;
    const auto* siblings = &rules.nodes;
    const auto parts = path.split(QLatin1Char('/'));
    const RuleNode* found = nullptr;
    for (const auto& part : parts) {
        bool valid = false;
        const int index = part.toInt(&valid);
        if (!valid || index < 0 || static_cast<std::size_t>(index) >= siblings->size())
            return nullptr;
        found = &(*siblings)[static_cast<std::size_t>(index)];
        siblings = &found->children;
    }
    return found;
}

// 규칙 트리를 화면용 깊이 우선 목록으로 펼치는 함수.
void appendNodes(QVariantList& output, const std::vector<RuleNode>& nodes,
                 const QString& parentPath, int depth) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto& node = nodes[index];
        const QString path = parentPath.isEmpty() ? QString::number(index)
            : parentPath + QLatin1Char('/') + QString::number(index);
        const QString title = node.otherwise ? QStringLiteral("그 외")
            : QString::fromStdString(node.id);
        output.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("title"), title},
            {QStringLiteral("enabled"), node.enabled},
            {QStringLiteral("destination"), node.destination
                ? QString::fromStdWString(node.destination->relativePath.wstring()) : QString()},
            {QStringLiteral("exclude"), node.exclude},
            {QStringLiteral("childrenCount"), static_cast<int>(node.children.size())},
        });
        appendNodes(output, node.children, path, depth + 1);
    }
}

// 확장자 목록을 편집 화면의 쉼표 구분 문자열로 바꾸는 함수.
QString extensionText(const ExtensionPredicate& condition) {
    QStringList values;
    for (const auto& extension : condition.values)
        values.append(QString::fromStdString(extension));
    return values.join(QStringLiteral(", "));
}

// 노드 번호를 포함한 그래프 오류를 반환하는 함수.
Result<std::vector<RuleNode>> graphError(int id, const QString& description) {
    const auto message = id > 0
        ? QStringLiteral("노드 %1: %2").arg(id).arg(description) : description;
    return Result<std::vector<RuleNode>>::failure({
        "rules.graph_invalid", message.toStdString()});
}

// 화면의 블록과 선을 순서 있는 단일 조건 규칙 트리로 바꾸는 함수.
Result<std::vector<RuleNode>> graphRules(const QVariantList& blocks,
                                         const QVariantList& edges) {
    if (blocks.size() > 1000) return graphError(0, QStringLiteral("노드가 너무 많아요."));
    QHash<int, QVariantMap> byId;
    int startId = -1;
    for (const auto& item : blocks) {
        const auto block = item.toMap();
        bool valid = false;
        const int id = block.value(QStringLiteral("id")).toInt(&valid);
        if (!valid || id <= 0 || byId.contains(id))
            return graphError(id, QStringLiteral("노드 번호가 없거나 중복돼요."));
        byId.insert(id, block);
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("start")) {
            if (startId >= 0) return graphError(id, QStringLiteral("파일 입력 노드는 하나만 둘 수 있어요."));
            startId = id;
        }
    }
    if (startId < 0) return graphError(0, QStringLiteral("파일 입력 노드가 필요해요."));
    QHash<int, QVector<int>> children;
    QHash<int, int> parent;
    for (const auto& item : edges) {
        const auto edge = item.toMap();
        bool validFrom = false;
        bool validTo = false;
        const int from = edge.value(QStringLiteral("from")).toInt(&validFrom);
        const int to = edge.value(QStringLiteral("to")).toInt(&validTo);
        if (!validFrom || !validTo || !byId.contains(from) || !byId.contains(to))
            return graphError(0, QStringLiteral("존재하지 않는 노드로 이어진 선이 있어요."));
        const auto sourceType = byId.value(from).value(QStringLiteral("type")).toString();
        if (to == startId || sourceType == QStringLiteral("destination") ||
            sourceType == QStringLiteral("exclude"))
            return graphError(to, QStringLiteral("이 방향으로는 연결할 수 없어요."));
        if (parent.contains(to))
            return graphError(to, QStringLiteral("입력에는 부모를 하나만 연결할 수 있어요."));
        parent.insert(to, from);
        children[from].push_back(to);
    }
    for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
        if (it.key() != startId && !parent.contains(it.key()))
            return graphError(it.key(), QStringLiteral("파일 입력에서 이어지지 않은 노드예요."));
    }
    if (children.value(startId).isEmpty())
        return graphError(startId, QStringLiteral("분류 조건을 하나 이상 연결해 주세요."));

    QSet<int> visiting;
    QSet<int> visited;
    QSet<QString> ruleIds;
    std::function<Result<std::vector<RuleNode>>(int, int)> convertChildren;
    // 각 부모의 연결 순서를 형제 실행 순서로 삼아 규칙 노드를 만드는 함수.
    convertChildren = [&](int owner, int depth) -> Result<std::vector<RuleNode>> {
        if (depth > 32) return graphError(owner, QStringLiteral("규칙 단계가 너무 깊어요."));
        std::vector<RuleNode> converted;
        bool seenOtherwise = false;
        const auto siblings = children.value(owner);
        for (int position = 0; position < siblings.size(); ++position) {
            const int id = siblings.at(position);
            if (visiting.contains(id))
                return graphError(id, QStringLiteral("자기 자신으로 돌아가는 연결이 있어요."));
            const auto block = byId.value(id);
            const auto type = block.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("destination") || type == QStringLiteral("exclude"))
                return graphError(id, QStringLiteral("목적지·정리 제외는 조건 뒤에만 놓아 주세요."));
            if (type == QStringLiteral("otherwise")) {
                if (seenOtherwise || position != siblings.size() - 1)
                    return graphError(id, QStringLiteral("'그 외'는 같은 부모의 마지막에 하나만 둘 수 있어요."));
                seenOtherwise = true;
            } else if (seenOtherwise) {
                return graphError(id, QStringLiteral("'그 외' 뒤에는 조건을 둘 수 없어요."));
            }
            RuleNode node;
            const auto existingId = block.value(QStringLiteral("ruleId")).toString();
            const auto ruleId = existingId.isEmpty()
                ? QStringLiteral("graph-%1").arg(id) : existingId;
            if (!QRegularExpression(QStringLiteral("^[a-zA-Z0-9._-]+$"))
                     .match(ruleId).hasMatch() || ruleIds.contains(ruleId))
                return graphError(id, QStringLiteral("규칙 ID가 올바르지 않거나 중복돼요."));
            ruleIds.insert(ruleId);
            node.id = ruleId.toStdString();
            node.enabled = block.value(QStringLiteral("enabled"), true).toBool();
            node.otherwise = type == QStringLiteral("otherwise");
            const auto value = block.value(QStringLiteral("value")).toString().trimmed();
            if (type == QStringLiteral("extension")) {
                ExtensionPredicate extension;
                for (const auto& part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    QString token = part.trimmed().toLower();
                    if (token.startsWith(QLatin1Char('.'))) token.remove(0, 1);
                    if (!QRegularExpression(QStringLiteral("^[a-z0-9]+$")).match(token).hasMatch())
                        return graphError(id, QStringLiteral("확장자를 쉼표로 구분해 입력해 주세요."));
                    const auto name = token.toStdString();
                    if (std::find(extension.values.begin(), extension.values.end(), name) ==
                        extension.values.end()) extension.values.push_back(name);
                }
                if (extension.values.empty())
                    return graphError(id, QStringLiteral("확장자를 하나 이상 입력해 주세요."));
                node.atomicCondition = std::move(extension);
            } else if (type == QStringLiteral("name")) {
                if (value.isEmpty()) return graphError(id, QStringLiteral("포함할 이름을 입력해 주세요."));
                node.atomicCondition = NameContainsPredicate{value.toStdString()};
            } else if (type == QStringLiteral("size") || type == QStringLiteral("age")) {
                bool validNumber = false;
                const auto amount = value.toULongLong(&validNumber);
                if (!validNumber) return graphError(id, QStringLiteral("0 이상의 정수를 입력해 주세요."));
                const auto comparison = block.value(QStringLiteral("comparison")).toString();
                if (type == QStringLiteral("size")) {
                    const auto unit = block.value(QStringLiteral("unit")).toString();
                    const auto factor = sizeFactor(unit);
                    if (!factor ||
                        amount > 9007199254740991ULL / factor)
                        return graphError(id, QStringLiteral("크기 또는 단위가 올바르지 않아요."));
                    SizeComparison mode;
                    if (comparison == QStringLiteral("greaterThan")) mode = SizeComparison::GreaterThan;
                    else if (comparison == QStringLiteral("atLeast")) mode = SizeComparison::AtLeast;
                    else if (comparison == QStringLiteral("lessThan")) mode = SizeComparison::LessThan;
                    else if (comparison == QStringLiteral("atMost")) mode = SizeComparison::AtMost;
                    else return graphError(id, QStringLiteral("크기 비교 방식을 골라 주세요."));
                    node.atomicCondition = SizePredicate{mode, amount * factor};
                } else {
                    if (amount > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
                        return graphError(id, QStringLiteral("수정 경과일이 너무 커요."));
                    if (comparison != QStringLiteral("atLeast") &&
                        comparison != QStringLiteral("lessThan"))
                        return graphError(id, QStringLiteral("경과일 비교 방식을 골라 주세요."));
                    node.atomicCondition = ModifiedAgePredicate{
                        comparison == QStringLiteral("atLeast") ? AgeComparison::AtLeast
                                                                 : AgeComparison::LessThan,
                        static_cast<int>(amount)};
                }
            } else if (!node.otherwise) {
                return graphError(id, QStringLiteral("알 수 없는 조건 종류예요."));
            }
            visiting.insert(id);
            const auto next = children.value(id);
            if (next.size() == 1) {
                const int terminalId = next.front();
                const auto terminal = byId.value(terminalId);
                const auto terminalType = terminal.value(QStringLiteral("type")).toString();
                if (terminalType == QStringLiteral("destination") ||
                    terminalType == QStringLiteral("exclude")) {
                    if (!children.value(terminalId).isEmpty())
                        return graphError(terminalId, QStringLiteral("종착 노드 뒤에는 연결할 수 없어요."));
                    visited.insert(terminalId);
                    if (terminalType == QStringLiteral("exclude")) {
                        node.exclude = true;
                    } else {
                        const auto path = terminal.value(QStringLiteral("value")).toString().trimmed();
                        const std::filesystem::path relative{path.toStdWString()};
                        auto unsupportedTokens = path;
                        unsupportedTokens.replace(QStringLiteral("{yyyy}"), QString());
                        unsupportedTokens.replace(QStringLiteral("{MM}"), QString());
                        if (path.isEmpty() || relative.is_absolute() || relative.has_root_name() ||
                            relative.has_root_directory() || unsupportedTokens.contains(QLatin1Char('{')) ||
                            unsupportedTokens.contains(QLatin1Char('}'))) {
                            return graphError(terminalId, QStringLiteral("정리 기준 폴더 안의 상대 경로를 입력해 주세요."));
                        }
                        for (const auto& part : relative) {
                            if (part == L"..")
                                return graphError(terminalId, QStringLiteral("상위 폴더로 나갈 수 없어요."));
                        }
                        std::optional<CollisionPolicy> policy;
                        const auto policyName = terminal.value(
                            QStringLiteral("collisionPolicy"), QStringLiteral("inherit")).toString();
                        if (policyName == QStringLiteral("rename")) policy = CollisionPolicy::Rename;
                        else if (policyName == QStringLiteral("skip")) policy = CollisionPolicy::Skip;
                        else if (policyName == QStringLiteral("abort")) policy = CollisionPolicy::Abort;
                        else if (policyName != QStringLiteral("inherit"))
                            return graphError(terminalId, QStringLiteral("파일 이름 충돌 설정이 올바르지 않아요."));
                        node.destination = RuleDestination{relative, policy};
                    }
                }
            }
            if (!node.exclude && !node.destination) {
                if (next.isEmpty())
                    return graphError(id, QStringLiteral("목적지 또는 정리 제외 노드를 연결해 주세요."));
                for (const int child : next) {
                    const auto childType = byId.value(child).value(QStringLiteral("type")).toString();
                    if (childType == QStringLiteral("destination") ||
                        childType == QStringLiteral("exclude"))
                        return graphError(child, QStringLiteral("종착 노드는 조건 자식과 함께 둘 수 없어요."));
                }
                auto descendants = convertChildren(id, depth + 1);
                if (!descendants) return descendants;
                node.children = std::move(descendants.value());
            }
            visiting.remove(id);
            visited.insert(id);
            converted.push_back(std::move(node));
        }
        return Result<std::vector<RuleNode>>::success(std::move(converted));
    };
    auto result = convertChildren(startId, 0);
    if (!result) return result;
    if (visited.size() + 1 != byId.size())
        return graphError(0, QStringLiteral("순환하거나 연결되지 않은 노드가 있어요."));
    return result;
}

// 규칙 ID가 맞는 별도 배치 파일에서 유효한 노드 좌표만 읽는 함수.
QHash<QString, QPointF> readGraphPositions(const QString& path, const std::string& ruleSetId) {
    QHash<QString, QPointF> positions;
    if (path.isEmpty()) return positions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return positions;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return positions;
    const auto object = document.object();
    if (object.value(QStringLiteral("format")).toInt() != 1 ||
        object.value(QStringLiteral("ruleSetId")).toString() !=
            QString::fromStdString(ruleSetId)) return positions;
    const auto saved = object.value(QStringLiteral("positions")).toObject();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        const auto point = it.value().toObject();
        const auto xValue = point.value(QStringLiteral("x"));
        const auto yValue = point.value(QStringLiteral("y"));
        if (!xValue.isDouble() || !yValue.isDouble()) continue;
        const double x = xValue.toDouble();
        const double y = yValue.toDouble();
        if (std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 10000000 &&
            std::abs(y) <= 10000000) positions.insert(it.key(), QPointF(x, y));
    }
    return positions;
}

// 저장된 좌표가 있으면 자동 배치 좌표 대신 해당 위치를 쓰는 함수.
QPointF graphPosition(const QHash<QString, QPointF>& saved, const QString& key,
                      double x, double y) {
    return saved.value(key, QPointF(x, y));
}

// 충돌 정책을 화면용 문자열로 바꾸는 함수.
QString policyName(const std::optional<CollisionPolicy>& policy) {
    if (!policy) return QStringLiteral("inherit");
    switch (*policy) {
    case CollisionPolicy::Rename: return QStringLiteral("rename");
    case CollisionPolicy::Skip: return QStringLiteral("skip");
    case CollisionPolicy::Abort: return QStringLiteral("abort");
    }
    return QStringLiteral("inherit");
}

// 저장된 규칙 하나의 조건을 노드 화면의 입력 형태로 바꾸는 함수.
QVariantMap graphBlockFor(const RuleNode& node, int id) {
    QVariantMap block{{QStringLiteral("id"), id},
                      {QStringLiteral("ruleId"), QString::fromStdString(node.id)},
                      {QStringLiteral("enabled"), node.enabled}};
    if (node.otherwise) {
        block.insert(QStringLiteral("type"), QStringLiteral("otherwise"));
        block.insert(QStringLiteral("value"), QStringLiteral("조건 없음"));
    } else if (node.atomicCondition) {
        const auto& condition = *node.atomicCondition;
        if (const auto* extension = std::get_if<ExtensionPredicate>(&condition)) {
            block.insert(QStringLiteral("type"), QStringLiteral("extension"));
            block.insert(QStringLiteral("value"), extensionText(*extension));
        } else if (const auto* name = std::get_if<NameContainsPredicate>(&condition)) {
            block.insert(QStringLiteral("type"), QStringLiteral("name"));
            block.insert(QStringLiteral("value"), QString::fromStdString(name->text));
        } else if (const auto* size = std::get_if<SizePredicate>(&condition)) {
            block.insert(QStringLiteral("type"), QStringLiteral("size"));
            const auto [amount, unit] = sizeDisplay(size->bytes);
            block.insert(QStringLiteral("value"), QString::number(amount));
            block.insert(QStringLiteral("unit"), unit);
            QString comparison = QStringLiteral("greaterThan");
            if (size->comparison == SizeComparison::AtLeast) comparison = QStringLiteral("atLeast");
            else if (size->comparison == SizeComparison::LessThan) comparison = QStringLiteral("lessThan");
            else if (size->comparison == SizeComparison::AtMost) comparison = QStringLiteral("atMost");
            block.insert(QStringLiteral("comparison"), comparison);
        } else {
            const auto& age = std::get<ModifiedAgePredicate>(condition);
            block.insert(QStringLiteral("type"), QStringLiteral("age"));
            block.insert(QStringLiteral("value"), QString::number(age.days));
            block.insert(QStringLiteral("comparison"), age.comparison == AgeComparison::AtLeast
                ? QStringLiteral("atLeast") : QStringLiteral("lessThan"));
        }
    }
    return block;
}

// 규칙 트리를 자동 배치와 저장된 좌표를 사용해 화면 그래프로 펼치는 함수.
QVariantMap graphForRules(const RuleSet& rules,
                          const QHash<QString, QPointF>& savedPositions) {
    QVariantList blocks;
    QVariantList edges;
    int nextId = 2;
    int nextRow = 0;
    int incompleteLeaves = 0;
    std::function<std::pair<int, double>(const RuleNode&, int)> appendNode;
    // 자식의 화면 위치를 먼저 구해 부모를 그 가운데에 놓는 함수.
    appendNode = [&](const RuleNode& node, int depth) -> std::pair<int, double> {
        const int id = nextId++;
        auto block = graphBlockFor(node, id);
        double y = 0;
        if (!node.children.empty()) {
            double firstY = -1;
            double lastY = 0;
            for (const auto& child : node.children) {
                const auto [childId, childY] = appendNode(child, depth + 1);
                edges.append(QVariantMap{{QStringLiteral("from"), id},
                                         {QStringLiteral("to"), childId}});
                if (firstY < 0) firstY = childY;
                lastY = childY;
            }
            y = (firstY + lastY) / 2;
        } else {
            y = 170 + nextRow++ * 170;
            if (node.destination || node.exclude) {
                const int terminalId = nextId++;
                const auto terminalKey = QStringLiteral("terminal:") + QString::fromStdString(node.id);
                const auto terminalPosition = graphPosition(
                    savedPositions, terminalKey, 70 + (depth + 1) * 310, y);
                QVariantMap terminal{
                    {QStringLiteral("id"), terminalId},
                    {QStringLiteral("type"), node.exclude ? QStringLiteral("exclude")
                                                         : QStringLiteral("destination")},
                    {QStringLiteral("parentRuleId"), QString::fromStdString(node.id)},
                    {QStringLiteral("value"), node.exclude ? QStringLiteral("이 파일은 이동하지 않음")
                        : QString::fromStdWString(node.destination->relativePath.wstring())},
                    {QStringLiteral("collisionPolicy"), node.destination
                        ? policyName(node.destination->collisionPolicy) : QStringLiteral("inherit")},
                    {QStringLiteral("x"), terminalPosition.x()},
                    {QStringLiteral("y"), terminalPosition.y()}};
                blocks.append(terminal);
                edges.append(QVariantMap{{QStringLiteral("from"), id},
                                         {QStringLiteral("to"), terminalId}});
            } else {
                ++incompleteLeaves;
            }
        }
        const auto position = graphPosition(
            savedPositions, QStringLiteral("node:") + QString::fromStdString(node.id),
            70 + depth * 310, y);
        block.insert(QStringLiteral("x"), position.x());
        block.insert(QStringLiteral("y"), position.y());
        blocks.append(block);
        return {id, position.y()};
    };
    double firstRootY = -1;
    double lastRootY = 0;
    for (const auto& root : rules.nodes) {
        const auto [id, y] = appendNode(root, 1);
        edges.append(QVariantMap{{QStringLiteral("from"), 1}, {QStringLiteral("to"), id}});
        if (firstRootY < 0) firstRootY = y;
        lastRootY = y;
    }
    const auto startPosition = graphPosition(savedPositions, QStringLiteral("start"),
        70, firstRootY < 0 ? 220 : (firstRootY + lastRootY) / 2);
    blocks.prepend(QVariantMap{{QStringLiteral("id"), 1},
                               {QStringLiteral("type"), QStringLiteral("start")},
                               {QStringLiteral("value"), QStringLiteral("정리할 파일")},
                               {QStringLiteral("x"), startPosition.x()},
                               {QStringLiteral("y"), startPosition.y()}});
    return QVariantMap{{QStringLiteral("valid"), true},
                       {QStringLiteral("blocks"), blocks},
                       {QStringLiteral("edges"), edges},
                       {QStringLiteral("nextId"), nextId},
                       {QStringLiteral("incompleteLeaves"), incompleteLeaves}};
}

} // namespace

// 저장소에서 규칙을 읽어 독립된 편집 초안을 만드는 생성자.
RuleEditorController::RuleEditorController(IRuleSetRepository& reader, IRuleSetWriter& writer,
                                           QObject* parent)
    : RuleEditorController(reader, writer, parent, nullptr, {}) {}

// 이전 규칙 변환 계약도 함께 받아 편집 초안을 만드는 생성자.
RuleEditorController::RuleEditorController(IRuleSetRepository& reader, IRuleSetWriter& writer,
                                           QObject* parent, IRuleSetMigrator* migrator,
                                           QString graphLayoutPath)
    : RuleEditorController(reader, writer, parent, migrator,
                           std::move(graphLayoutPath), nullptr) {}

// 외부 파일 검증 계약까지 함께 받아 편집 초안을 만드는 생성자.
RuleEditorController::RuleEditorController(IRuleSetRepository& reader, IRuleSetWriter& writer,
                                           QObject* parent, IRuleSetMigrator* migrator,
                                           QString graphLayoutPath, IRuleSetFileReader* fileReader,
                                           TestRuleUseCase* ruleTester,
                                           IRuleSetFileWriter* fileWriter)
    : QObject(parent), reader_(reader), writer_(writer), migrator_(migrator), fileReader_(fileReader),
      ruleTester_(ruleTester), fileWriter_(fileWriter),
      graphLayoutPath_(std::move(graphLayoutPath)) { reload(); }

// 현재 초안의 모든 노드를 화면 표시 순서로 반환하는 함수.
QVariantList RuleEditorController::nodes() const {
    QVariantList result;
    if (loaded_) appendNodes(result, draft_.nodes, {}, 0);
    return result;
}

// 최근 상태 문구를 반환하는 함수.
QString RuleEditorController::message() const { return message_; }

// 저장하지 않은 편집 여부를 반환하는 함수.
bool RuleEditorController::dirty() const noexcept { return dirty_; }

// 최종적으로 분류되지 않은 파일의 전체 예외 목적지를 반환하는 함수.
QString RuleEditorController::globalDestination() const {
    return loaded_ && draft_.globalOtherwise
        ? QString::fromStdWString(draft_.globalOtherwise->relativePath.wstring()) : QString();
}

// 현재 파일이 편집 전 변환이 필요한 v2 규칙인지 반환하는 함수.
bool RuleEditorController::legacyRules() const noexcept { return legacyRules_; }

// 선택한 규칙의 조건과 목적지를 편집 필드 값으로 반환하는 함수.
QVariantMap RuleEditorController::detail(const QString& path) const {
    if (!loaded_) return {};
    const auto* node = nodeAt(draft_, path);
    if (!node) return {};
    QVariantMap result{
        {QStringLiteral("id"), QString::fromStdString(node->id)},
        {QStringLiteral("enabled"), node->enabled},
        {QStringLiteral("otherwise"), node->otherwise},
        {QStringLiteral("destination"), node->destination
            ? QString::fromStdWString(node->destination->relativePath.wstring()) : QString()},
        {QStringLiteral("exclude"), node->exclude},
        {QStringLiteral("childrenCount"), static_cast<int>(node->children.size())},
    };
    if (!node->atomicCondition) return result;
    const auto& condition = *node->atomicCondition;
    if (const auto* extension = std::get_if<ExtensionPredicate>(&condition)) {
        result.insert(QStringLiteral("type"), QStringLiteral("extension"));
        result.insert(QStringLiteral("value"), extensionText(*extension));
    } else if (const auto* name = std::get_if<NameContainsPredicate>(&condition)) {
        result.insert(QStringLiteral("type"), QStringLiteral("nameContains"));
        result.insert(QStringLiteral("value"), QString::fromStdString(name->text));
    } else if (const auto* size = std::get_if<SizePredicate>(&condition)) {
        result.insert(QStringLiteral("type"), QStringLiteral("size"));
        const auto [amount, unit] = sizeDisplay(size->bytes);
        result.insert(QStringLiteral("value"), QString::number(amount));
        result.insert(QStringLiteral("sizeUnit"), unit);
        const char* comparison = "greaterThan";
        if (size->comparison == SizeComparison::AtLeast) comparison = "atLeast";
        if (size->comparison == SizeComparison::LessThan) comparison = "lessThan";
        if (size->comparison == SizeComparison::AtMost) comparison = "atMost";
        result.insert(QStringLiteral("comparison"), QString::fromLatin1(comparison));
    } else {
        const auto& age = std::get<ModifiedAgePredicate>(condition);
        result.insert(QStringLiteral("type"), QStringLiteral("modifiedAgeDays"));
        result.insert(QStringLiteral("comparison"), age.comparison == AgeComparison::AtLeast
            ? QStringLiteral("atLeast") : QStringLiteral("lessThan"));
        result.insert(QStringLiteral("value"), age.days);
    }
    return result;
}

// 선택한 규칙을 저장본과 분리된 초안에서 수정하는 함수.
bool RuleEditorController::edit(const QString& path, const QString& id, bool enabled,
                                const QString& type, const QString& comparison,
                                const QString& value, const QString& sizeUnit,
                                const QString& destination) {
    auto* node = loaded_ ? nodeAt(draft_, path) : nullptr;
    if (!node) return false;
    static const QRegularExpression validId(QStringLiteral("^[a-zA-Z0-9._-]+$"));
    if (!validId.match(id.trimmed()).hasMatch()) {
        message_ = QStringLiteral("규칙 이름은 영문·숫자·점·밑줄·하이픈만 사용할 수 있어요.");
        emit changed();
        return false;
    }
    std::optional<AtomicRuleCondition> condition;
    if (!node->otherwise) {
        if (type == QStringLiteral("extension")) {
            ExtensionPredicate parsed;
            for (const auto& part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                QString extension = part.trimmed().toLower();
                if (extension.startsWith(QLatin1Char('.'))) extension.remove(0, 1);
                if (!QRegularExpression(QStringLiteral("^[a-z0-9]+$"))
                         .match(extension).hasMatch()) {
                    message_ = QStringLiteral("확장자를 쉼표로 구분해 입력해 주세요.");
                    emit changed();
                    return false;
                }
                const auto converted = extension.toStdString();
                if (std::find(parsed.values.begin(), parsed.values.end(), converted) ==
                    parsed.values.end()) parsed.values.push_back(converted);
            }
            if (!parsed.values.empty()) condition = std::move(parsed);
        } else if (type == QStringLiteral("nameContains")) {
            if (!value.trimmed().isEmpty())
                condition = NameContainsPredicate{value.trimmed().toStdString()};
        } else if (type == QStringLiteral("size")) {
            bool valid = false;
            const auto amount = value.trimmed().toULongLong(&valid);
            const auto multiplier = sizeFactor(sizeUnit);
            if (!valid || !multiplier || amount > 9007199254740991ULL / multiplier) {
                message_ = QStringLiteral("크기는 0 이상의 정수로 입력해 주세요.");
                emit changed();
                return false;
            }
            SizeComparison mode = SizeComparison::GreaterThan;
            if (comparison == QStringLiteral("atLeast")) mode = SizeComparison::AtLeast;
            else if (comparison == QStringLiteral("lessThan")) mode = SizeComparison::LessThan;
            else if (comparison == QStringLiteral("atMost")) mode = SizeComparison::AtMost;
            else if (comparison != QStringLiteral("greaterThan")) return false;
            condition = SizePredicate{mode, amount * multiplier};
        } else if (type == QStringLiteral("modifiedAgeDays")) {
            bool valid = false;
            const int days = value.trimmed().toInt(&valid);
            if (!valid || days < 0) {
                message_ = QStringLiteral("수정 후 지난 일수를 0 이상의 정수로 입력해 주세요.");
                emit changed();
                return false;
            }
            if (comparison != QStringLiteral("atLeast") &&
                comparison != QStringLiteral("lessThan")) return false;
            condition = ModifiedAgePredicate{
                comparison == QStringLiteral("atLeast") ? AgeComparison::AtLeast
                                                         : AgeComparison::LessThan, days};
        }
        if (!condition) {
            message_ = QStringLiteral("조건 종류를 고르고 값을 입력해 주세요.");
            emit changed();
            return false;
        }
    }
    if (!node->children.empty() && !destination.trimmed().isEmpty()) {
        message_ = QStringLiteral("자식이 있는 가지에는 목적지를 정할 수 없어요.");
        emit changed();
        return false;
    }
    if (node->exclude && !destination.trimmed().isEmpty()) {
        message_ = QStringLiteral("정리 제외 규칙에는 목적지를 지정할 수 없어요.");
        emit changed();
        return false;
    }
    node->id = id.trimmed().toStdString();
    node->enabled = enabled;
    node->atomicCondition = std::move(condition);
    if (destination.trimmed().isEmpty()) node->destination.reset();
    else node->destination = RuleDestination{
        std::filesystem::path(destination.trimmed().toStdWString()),
        node->destination ? node->destination->collisionPolicy : std::nullopt};
    dirty_ = true;
    message_ = QStringLiteral("초안만 바뀌었어요. 저장해야 실제 분류에 적용됩니다.");
    emit changed();
    return true;
}

// 루트나 내부 가지에 새 규칙을 추가하는 함수.
bool RuleEditorController::add(const QString& parentPath) {
    auto* parent = parentPath.isEmpty() ? nullptr : nodeAt(draft_, parentPath);
    auto* siblings = loaded_ ? siblingsAt(draft_, parentPath) : nullptr;
    if (!siblings || (parent && (parent->destination || parent->exclude))) {
        message_ = QStringLiteral("목적지나 정리 제외로 끝나는 규칙에는 자식을 추가할 수 없어요.");
        emit changed();
        return false;
    }
    RuleNode node;
    node.id = "rule-" + QUuid::createUuid().toString(QUuid::Id128).left(8).toStdString();
    const auto otherwise = std::find_if(siblings->begin(), siblings->end(),
                                        [](const RuleNode& entry) { return entry.otherwise; });
    siblings->insert(otherwise, std::move(node));
    dirty_ = true;
    message_ = QStringLiteral("새 규칙을 추가했어요. 저장 전에 조건을 입력해 주세요.");
    emit changed();
    return true;
}

// 같은 높이의 조건이 모두 빗나갈 때 적용할 예외 가지를 추가하는 함수.
bool RuleEditorController::addOtherwise(const QString& parentPath) {
    auto* parent = parentPath.isEmpty() ? nullptr : nodeAt(draft_, parentPath);
    auto* siblings = loaded_ ? siblingsAt(draft_, parentPath) : nullptr;
    if (!siblings || (parent && (parent->destination || parent->exclude))) return false;
    if (std::any_of(siblings->begin(), siblings->end(),
                    [](const RuleNode& entry) { return entry.otherwise; })) {
        message_ = QStringLiteral("같은 높이에는 '그 외'를 하나만 둘 수 있어요.");
        emit changed();
        return false;
    }
    RuleNode node;
    node.id = "else-" + QUuid::createUuid().toString(QUuid::Id128).left(8).toStdString();
    node.otherwise = true;
    siblings->push_back(std::move(node));
    dirty_ = true;
    message_ = QStringLiteral("'그 외' 가지를 추가했어요.");
    emit changed();
    return true;
}

// 예외 가지는 마지막에 두고 일반 규칙만 형제 사이에서 이동하는 함수.
bool RuleEditorController::move(const QString& path, int direction) {
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    auto* siblings = loaded_ ? siblingsAt(draft_, slash < 0 ? QString() : path.left(slash)) : nullptr;
    bool valid = false;
    const int index = path.mid(slash + 1).toInt(&valid);
    const int target = index + direction;
    if (!siblings || !valid || (direction != -1 && direction != 1) ||
        index < 0 || target < 0 || static_cast<std::size_t>(index) >= siblings->size() ||
        static_cast<std::size_t>(target) >= siblings->size() ||
        (*siblings)[static_cast<std::size_t>(index)].otherwise ||
        (*siblings)[static_cast<std::size_t>(target)].otherwise) return false;
    std::swap((*siblings)[static_cast<std::size_t>(index)],
              (*siblings)[static_cast<std::size_t>(target)]);
    dirty_ = true;
    message_ = QStringLiteral("순서를 바꿨어요. 위 조건부터 검사합니다.");
    emit changed();
    return true;
}

// 선택한 규칙과 자식을 초안에서 제거하는 함수.
bool RuleEditorController::remove(const QString& path) {
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    auto* siblings = loaded_ ? siblingsAt(draft_, slash < 0 ? QString() : path.left(slash)) : nullptr;
    bool valid = false;
    const int index = path.mid(slash + 1).toInt(&valid);
    if (!siblings || !valid || index < 0 || static_cast<std::size_t>(index) >= siblings->size())
        return false;
    siblings->erase(siblings->begin() + index);
    dirty_ = true;
    message_ = QStringLiteral("초안에서 제거했어요. 저장 전에는 되돌릴 수 있습니다.");
    emit changed();
    return true;
}

// 검증된 화면 그래프를 기존 저장본 대신 편집 초안에만 반영하는 함수.
bool RuleEditorController::importGraph(const QVariantList& blocks, const QVariantList& edges) {
    if (!loaded_) {
        message_ = QStringLiteral("현재 규칙을 읽거나 새 형식으로 변환한 뒤 시도해 주세요.");
        emit changed();
        return false;
    }
    auto converted = graphRules(blocks, edges);
    if (!converted) {
        message_ = QString::fromStdString(converted.error().message);
        emit changed();
        return false;
    }
    draft_.nodes = std::move(converted.value());
    dirty_ = true;
    message_ = QStringLiteral("노드 그래프를 규칙 초안으로 바꿨어요. 기존 편집기에서 확인한 뒤 저장해 주세요.");
    emit changed();
    return true;
}

// 선택한 로컬 파일을 검증하고 오류 위치를 돌려주며 성공할 때만 초안을 바꾸는 함수.
QVariantMap RuleEditorController::importJsonFile(const QUrl& fileUrl) {
    if (!fileReader_ || !fileUrl.isLocalFile()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("location"), QStringLiteral("파일")},
                {QStringLiteral("reason"), QStringLiteral("로컬 JSON 파일만 불러올 수 있어요.")}};
    }
    const auto result = fileReader_->loadFile(
        std::filesystem::path(fileUrl.toLocalFile().toStdWString()));
    if (!result) {
        const QString detail = QString::fromStdString(result.error().message);
        const qsizetype separator = detail.indexOf(QStringLiteral(": "));
        const bool hasLocation = separator >= 0 &&
            (detail.startsWith(QLatin1Char('$')) || detail.startsWith(QStringLiteral("line ")) ||
             detail.startsWith(QStringLiteral("root:")));
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("location"), hasLocation ? detail.left(separator)
                                                          : QStringLiteral("규칙 파일")},
                {QStringLiteral("reason"), hasLocation ? detail.mid(separator + 2) : detail},
                {QStringLiteral("code"), QString::fromStdString(result.error().code)}};
    }
    if (result.value().schemaVersion != 3) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("location"), QStringLiteral("$.schemaVersion")},
                {QStringLiteral("reason"), QStringLiteral("v3 형식의 규칙 파일만 불러올 수 있어요.")}};
    }
    const auto previewGraph = graphForRules(result.value(), {});
    const auto checkedGraph = graphRules(previewGraph.value(QStringLiteral("blocks")).toList(),
                                         previewGraph.value(QStringLiteral("edges")).toList());
    if (!checkedGraph) {
        const QString detail = QString::fromStdString(checkedGraph.error().message);
        const qsizetype separator = detail.indexOf(QStringLiteral(": "));
        QString location = separator >= 0 ? detail.left(separator) : QStringLiteral("규칙 트리");
        if (location.startsWith(QStringLiteral("노드 "))) {
            const int graphId = location.mid(3).toInt();
            for (const auto& item : previewGraph.value(QStringLiteral("blocks")).toList()) {
                const auto block = item.toMap();
                if (block.value(QStringLiteral("id")).toInt() != graphId) continue;
                const QString ruleId = block.value(QStringLiteral("ruleId"),
                                      block.value(QStringLiteral("parentRuleId"))).toString();
                if (!ruleId.isEmpty()) location = QStringLiteral("규칙 ID: %1").arg(ruleId);
                break;
            }
        }
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("location"), location},
                {QStringLiteral("reason"), separator >= 0 ? detail.mid(separator + 2) : detail}};
    }
    draft_ = result.value();
    loaded_ = true;
    legacyRules_ = false;
    dirty_ = true;
    importedDraft_ = true;
    message_ = QStringLiteral("외부 규칙을 편집 초안으로 불러왔어요. 전체 저장 전까지 적용되지 않습니다.");
    emit changed();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("location"), QString()},
            {QStringLiteral("reason"), QString()}};
}

// 현재 화면 그래프를 검증한 후 활성 규칙과 초안을 바꾸지 않고 JSON 파일로 저장하는 함수.
QVariantMap RuleEditorController::exportGraph(const QVariantList& blocks,
                                              const QVariantList& edges,
                                              const QUrl& fileUrl) const {
    if (!loaded_ || !fileWriter_) return {{QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("현재 규칙을 내보낼 수 없어요.")}};
    if (!fileUrl.isLocalFile()) return {{QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("로컬 JSON 파일만 내보낼 수 있어요.")}};
    const auto checked = graphRules(blocks, edges);
    if (!checked) return {{QStringLiteral("ok"), false},
        {QStringLiteral("error"), QString::fromStdString(checked.error().message)}};
    RuleSet exported = draft_;
    exported.schemaVersion = 3;
    exported.nodes = std::move(checked.value());
    const auto saved = fileWriter_->saveFile(
        std::filesystem::path(fileUrl.toLocalFile().toStdWString()), exported);
    if (!saved) return {{QStringLiteral("ok"), false},
        {QStringLiteral("error"), QString::fromStdString(saved.error().message)}};
    return {{QStringLiteral("ok"), true}};
}

// 미저장 그래프를 검증한 뒤 선택 파일 또는 가상 파일의 분류 경로만 계산하는 함수.
QVariantMap RuleEditorController::testGraph(const QVariantList& blocks, const QVariantList& edges,
                                            const QUrl& fileUrl, const QString& fileName,
                                            const QString& sizeBytes, const QString& ageDays) const {
    if (!loaded_ || !ruleTester_) return {{QStringLiteral("ok"), false},
                          {QStringLiteral("error"), QStringLiteral("규칙을 먼저 불러와 주세요.")}};
    const auto checked = graphRules(blocks, edges);
    if (!checked) return {{QStringLiteral("ok"), false},
                          {QStringLiteral("error"), QString::fromStdString(checked.error().message)}};

    const auto nowMs = QDateTime::currentMSecsSinceEpoch();
    FileFact fact;
    std::optional<std::filesystem::path> selectedPath;
    if (!fileUrl.isEmpty()) {
        if (!fileUrl.isLocalFile()) return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("로컬 파일 하나만 시험할 수 있어요.")}};
        selectedPath = std::filesystem::path(fileUrl.toLocalFile().toStdWString());
    } else {
        const auto name = fileName.trimmed();
        bool sizeValid = false;
        bool ageValid = false;
        const auto bytes = sizeBytes.trimmed().toULongLong(&sizeValid);
        const auto days = ageDays.trimmed().toLongLong(&ageValid);
        if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
            name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
            !sizeValid || bytes > 9007199254740991ULL || !ageValid || days < 0 || days > 36500) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral(
                        "파일 이름, 크기(B), 수정 후 경과일(0~36500)을 확인해 주세요.")}};
        }
        fact.relativePath = std::filesystem::path(name.toStdWString());
        fact.fingerprint.size = bytes;
        fact.fingerprint.modifiedUtcMs = nowMs - days * 86400000LL;
    }

    RuleSet rules = draft_;
    rules.schemaVersion = 3;
    rules.nodes = checked.value();
    const auto trace = selectedPath
        ? ruleTester_->testSelectedFile(*selectedPath, rules, nowMs)
        : ruleTester_->testMetadata(fact, rules, nowMs);
    if (!trace) return {{QStringLiteral("ok"), false},
                        {QStringLiteral("error"), QString::fromStdString(trace.error().message)}};

    QVariantList path;
    for (const auto& id : trace.value().selectedNodeIds)
        path.append(QString::fromStdString(id));
    const auto& decision = trace.value().decision;
    const QString outcome = !decision ? QStringLiteral("unclassified")
        : decision->excluded ? QStringLiteral("excluded") : QStringLiteral("destination");
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("outcome"), outcome},
            {QStringLiteral("matchedRuleId"), decision
                ? QString::fromStdString(decision->matchedRuleId) : QString()},
            {QStringLiteral("destination"), decision && !decision->excluded
                ? QString::fromStdWString(decision->destinationRelativePath.wstring()) : QString()},
            {QStringLiteral("path"), path},
            {QStringLiteral("fileName"), fileUrl.isEmpty() ? fileName.trimmed()
                : fileUrl.fileName()}};
}

// 현재 규칙 초안을 별도 배치 파일의 좌표와 합쳐 화면 그래프로 반환하는 함수.
QVariantMap RuleEditorController::graph() const {
    if (!loaded_) return QVariantMap{{QStringLiteral("valid"), false},
                                     {QStringLiteral("message"), message_}};
    return graphForRules(draft_, importedDraft_ ? QHash<QString, QPointF>{}
                                                : readGraphPositions(graphLayoutPath_, draft_.id));
}

// 노드 ID별 좌표만 규칙 파일과 독립된 JSON 파일에 원자적으로 저장하는 함수.
bool RuleEditorController::saveGraphLayout(const QVariantList& blocks,
                                           const QVariantList& edges) {
    if (!loaded_ || dirty_ || graphLayoutPath_.isEmpty()) {
        message_ = QStringLiteral("규칙을 먼저 저장한 뒤 노드 배치를 저장해 주세요.");
        emit changed();
        return false;
    }
    QHash<int, QVariantMap> byId;
    QHash<int, int> parent;
    for (const auto& item : blocks) {
        const auto block = item.toMap();
        byId.insert(block.value(QStringLiteral("id")).toInt(), block);
    }
    for (const auto& item : edges) {
        const auto edge = item.toMap();
        parent.insert(edge.value(QStringLiteral("to")).toInt(),
                      edge.value(QStringLiteral("from")).toInt());
    }
    QJsonObject positions;
    for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
        const auto block = it.value();
        const auto type = block.value(QStringLiteral("type")).toString();
        QString key;
        if (type == QStringLiteral("start")) {
            key = QStringLiteral("start");
        } else if (type == QStringLiteral("destination") || type == QStringLiteral("exclude")) {
            const auto parentBlock = byId.value(parent.value(it.key(), -1));
            const auto ruleId = parentBlock.value(QStringLiteral("ruleId")).toString();
            key = QStringLiteral("terminal:") + (ruleId.isEmpty()
                ? QStringLiteral("graph-%1").arg(parent.value(it.key(), -1)) : ruleId);
        } else {
            const auto ruleId = block.value(QStringLiteral("ruleId")).toString();
            key = QStringLiteral("node:") + (ruleId.isEmpty()
                ? QStringLiteral("graph-%1").arg(it.key()) : ruleId);
        }
        bool validX = false;
        bool validY = false;
        const double x = block.value(QStringLiteral("x")).toDouble(&validX);
        const double y = block.value(QStringLiteral("y")).toDouble(&validY);
        if (!validX || !validY || !std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > 10000000 || std::abs(y) > 10000000) {
            message_ = QStringLiteral("노드 좌표가 올바르지 않아 배치를 저장하지 못했어요.");
            emit changed();
            return false;
        }
        positions.insert(key, QJsonObject{{QStringLiteral("x"), x},
                                          {QStringLiteral("y"), y}});
    }
    if (!QDir().mkpath(QFileInfo(graphLayoutPath_).absolutePath())) {
        message_ = QStringLiteral("노드 배치 저장 폴더를 만들지 못했어요.");
        emit changed();
        return false;
    }
    QSaveFile file(graphLayoutPath_);
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("format"), 1},
        {QStringLiteral("ruleSetId"), QString::fromStdString(draft_.id)},
        {QStringLiteral("positions"), positions}});
    const auto encoded = document.toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() ||
        !file.commit()) {
        message_ = QStringLiteral("노드 배치 파일을 저장하지 못했어요.");
        emit changed();
        return false;
    }
    message_ = QStringLiteral("노드 위치만 저장했어요. 분류 순서와 규칙 내용은 바뀌지 않습니다.");
    emit changed();
    return true;
}

// 저장하지 않은 초안을 버리고 활성 규칙 파일을 다시 읽는 함수.
void RuleEditorController::reload() {
    const auto result = reader_.load();
    importedDraft_ = false;
    legacyRules_ = result && result.value().schemaVersion == 2;
    if (!result || result.value().schemaVersion != 3) {
        loaded_ = false;
        message_ = result ? QStringLiteral("이전 v2 규칙입니다. 분류는 계속되지만 편집하려면 안전한 형식 변환이 필요해요.")
                          : QStringLiteral("규칙을 읽지 못했어요: %1")
                                .arg(QString::fromStdString(result.error().message));
    } else {
        draft_ = result.value();
        loaded_ = true;
        dirty_ = false;
        message_ = QStringLiteral("현재 저장된 규칙을 불러왔어요.");
    }
    emit changed();
}

// 사용자가 선택했을 때에만 v2 규칙의 백업과 안전한 변환을 요청하는 함수.
bool RuleEditorController::migrateLegacyRules() {
    if (!legacyRules_ || !migrator_) return false;
    const auto result = migrator_->migrateV2ToV3();
    if (!result) {
        message_ = result.error().code == "rules.migration_requires_review"
            ? QStringLiteral("기존 조건의 뜻을 그대로 유지하며 자동 변환할 수 없어요. 원본 규칙은 그대로 두었어요.")
            : QStringLiteral("기존 규칙을 변환하지 못했어요: %1")
                  .arg(QString::fromStdString(result.error().message));
        emit changed();
        return false;
    }
    reload();
    message_ = QStringLiteral("원본 규칙을 백업하고 새 형식으로 변환했어요.");
    emit changed();
    emit rulesSaved();
    return loaded_;
}

// 초안을 검증·저장하고 저장 성공 시에만 정식 미리보기를 무효화하는 함수.
bool RuleEditorController::save() {
    if (!loaded_) return false;
    const auto previousVersion = draft_.version;
    draft_.version = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    const auto result = writer_.save(draft_);
    if (!result) {
        draft_.version = previousVersion;
        message_ = QStringLiteral("저장하지 못했어요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit changed();
        return false;
    }
    dirty_ = false;
    importedDraft_ = false;
    message_ = QStringLiteral("규칙을 저장했어요. 새 미리보기에 적용됩니다.");
    emit changed();
    emit rulesSaved();
    return true;
}

// 빈 값이면 전체 예외를 끄고 아니면 지정한 상대 경로를 초안에 보관하는 함수.
void RuleEditorController::setGlobalDestination(const QString& destination) {
    if (!loaded_) return;
    if (destination.trimmed().isEmpty()) draft_.globalOtherwise.reset();
    else draft_.globalOtherwise = RuleDestination{
        std::filesystem::path(destination.trimmed().toStdWString()), std::nullopt};
    dirty_ = true;
    message_ = QStringLiteral("전체 예외를 초안에서 바꿨어요. 저장해야 적용됩니다.");
    emit changed();
}

} // namespace nookbit
