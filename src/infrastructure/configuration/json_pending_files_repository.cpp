#include "infrastructure/configuration/json_pending_files_repository.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <utility>

namespace nookbit {
namespace {

// 빈 값·상대 경로·루트 경로를 대기 파일 경로에서 거부하는 함수.
bool validPath(const QString& path) {
    const QFileInfo info(path);
    return !path.isEmpty() && !path.contains(QChar::Null) && info.isAbsolute() &&
           !QDir(path).isRoot();
}

} // namespace

// 저장 파일의 경로를 보관하는 생성자.
JsonPendingFilesRepository::JsonPendingFilesRepository(QString storagePath)
    : storagePath_(std::move(storagePath)) {}

// 문서 전체를 검증한 뒤 대기 경로를 읽는 함수.
Result<std::vector<std::filesystem::path>> JsonPendingFilesRepository::load() const {
    const auto loaded = loadEntries();
    if (!loaded) return Result<std::vector<std::filesystem::path>>::failure(loaded.error());
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : loaded.value()) paths.push_back(entry.path);
    return Result<std::vector<std::filesystem::path>>::success(std::move(paths));
}

// 기존 경로 문서와 선택적 폴더 흐름을 모두 검증하여 읽는 함수.
Result<std::vector<PendingFileEntry>> JsonPendingFilesRepository::loadEntries() const {
    if (!QFileInfo::exists(storagePath_))
        return Result<std::vector<PendingFileEntry>>::success({});
    QFile file(storagePath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        return Result<std::vector<PendingFileEntry>>::failure({
            "pending.read_failed", "Cannot read the pending-list document.",
        });
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        object.value(QStringLiteral("schemaVersion")) != QJsonValue(1) ||
        !object.value(QStringLiteral("paths")).isArray()) {
        return Result<std::vector<PendingFileEntry>>::failure({
            "pending.invalid_document", "Invalid or unsupported pending-list document.",
        });
    }
    std::vector<PendingFileEntry> entries;
    QHash<QString, int> indices;
    for (const auto& value : object.value(QStringLiteral("paths")).toArray()) {
        if (!value.isString() || !validPath(value.toString())) {
            return Result<std::vector<PendingFileEntry>>::failure({
                "pending.invalid_path", "Pending-list entries must be absolute file paths.",
            });
        }
        const auto path = QDir::cleanPath(value.toString());
        indices.insert(path.toCaseFolded(), static_cast<int>(entries.size()));
        entries.push_back({std::filesystem::path(path.toStdWString()), {}, {}});
    }
    if (object.contains(QStringLiteral("folderSelections"))) {
        if (!object.value(QStringLiteral("folderSelections")).isArray())
            return Result<std::vector<PendingFileEntry>>::failure({"pending.invalid_selection", "Folder selections must be an array."});
        QSet<QString> seen;
        for (const auto& value : object.value(QStringLiteral("folderSelections")).toArray()) {
            const auto selection = value.toObject();
            const auto path = QDir::cleanPath(selection.value(QStringLiteral("path")).toString()).toCaseFolded();
            const auto flow = selection.value(QStringLiteral("flowId")).toString();
            const auto rules = selection.value(QStringLiteral("ruleSetId")).toString();
            const QRegularExpression id(QStringLiteral("^[a-zA-Z0-9._-]+$"));
            if (!value.isObject() || selection.size() != 3 || !indices.contains(path) || seen.contains(path) ||
                !id.match(flow).hasMatch() || !id.match(rules).hasMatch())
                return Result<std::vector<PendingFileEntry>>::failure({"pending.invalid_selection", "Invalid folder processing selection."});
            seen.insert(path);
            auto& entry = entries[indices.value(path)];
            entry.folderFlowId = flow.toStdString();
            entry.ruleSetId = rules.toStdString();
        }
    }
    return Result<std::vector<PendingFileEntry>>::success(std::move(entries));
}

// 경로를 검증하고 이전 문서를 원자적으로 교체하는 함수.
Result<void> JsonPendingFilesRepository::save(const std::vector<std::filesystem::path>& paths) const {
    std::vector<PendingFileEntry> entries;
    for (const auto& path : paths) entries.push_back({path, {}, {}});
    return saveEntries(entries);
}

// 경로와 폴더 처리 선택을 검증하고 이전 문서를 한 번에 교체하는 함수.
Result<void> JsonPendingFilesRepository::saveEntries(const std::vector<PendingFileEntry>& entries) const {
    QJsonArray array;
    QJsonArray selections;
    for (const auto& entry : entries) {
        const auto text = QString::fromStdWString(entry.path.wstring());
        if (!validPath(text)) return Result<void>::failure({
            "pending.invalid_path", "Pending-list entries must be absolute file paths.",
        });
        array.append(QDir::cleanPath(text));
        if (!entry.folderFlowId.empty() || !entry.ruleSetId.empty()) {
            const auto flow = QString::fromStdString(entry.folderFlowId);
            const auto rules = QString::fromStdString(entry.ruleSetId);
            const QRegularExpression id(QStringLiteral("^[a-zA-Z0-9._-]+$"));
            if (!id.match(flow).hasMatch() || !id.match(rules).hasMatch())
                return Result<void>::failure({"pending.invalid_selection", "Invalid folder processing selection."});
            selections.append(QJsonObject{{QStringLiteral("path"), QDir::cleanPath(text)},
                {QStringLiteral("flowId"), flow}, {QStringLiteral("ruleSetId"), rules}});
        }
    }
    QJsonObject object{
        {QStringLiteral("schemaVersion"), 1}, {QStringLiteral("paths"), array},
    };
    if (!selections.isEmpty()) object.insert(QStringLiteral("folderSelections"), selections);
    const auto bytes = QJsonDocument(object).toJson();
    if (bytes.size() > 16 * 1024 * 1024 || !QFileInfo(storagePath_).isAbsolute() ||
        !QDir().mkpath(QFileInfo(storagePath_).absolutePath())) {
        return Result<void>::failure({"pending.path_invalid", "Cannot prepare the pending-list location."});
    }
    QSaveFile file(storagePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return Result<void>::failure({"pending.write_failed", "Cannot save the pending list atomically."});
    }
    return Result<void>::success();
}

} // namespace nookbit
