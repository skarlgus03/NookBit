#include "infrastructure/configuration/json_pending_files_repository.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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
    if (!QFileInfo::exists(storagePath_))
        return Result<std::vector<std::filesystem::path>>::success({});
    QFile file(storagePath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        return Result<std::vector<std::filesystem::path>>::failure({
            "pending.read_failed", "Cannot read the pending-list document.",
        });
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        object.value(QStringLiteral("schemaVersion")) != QJsonValue(1) ||
        !object.value(QStringLiteral("paths")).isArray()) {
        return Result<std::vector<std::filesystem::path>>::failure({
            "pending.invalid_document", "Invalid or unsupported pending-list document.",
        });
    }
    std::vector<std::filesystem::path> paths;
    for (const auto& value : object.value(QStringLiteral("paths")).toArray()) {
        if (!value.isString() || !validPath(value.toString())) {
            return Result<std::vector<std::filesystem::path>>::failure({
                "pending.invalid_path", "Pending-list entries must be absolute file paths.",
            });
        }
        paths.emplace_back(QDir::cleanPath(value.toString()).toStdWString());
    }
    return Result<std::vector<std::filesystem::path>>::success(std::move(paths));
}

// 경로를 검증하고 이전 문서를 원자적으로 교체하는 함수.
Result<void> JsonPendingFilesRepository::save(const std::vector<std::filesystem::path>& paths) const {
    QJsonArray array;
    for (const auto& path : paths) {
        const auto text = QString::fromStdWString(path.wstring());
        if (!validPath(text)) return Result<void>::failure({
            "pending.invalid_path", "Pending-list entries must be absolute file paths.",
        });
        array.append(QDir::cleanPath(text));
    }
    const auto bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1}, {QStringLiteral("paths"), array},
    }).toJson();
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
