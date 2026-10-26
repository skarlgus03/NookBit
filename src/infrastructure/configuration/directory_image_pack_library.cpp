#include "infrastructure/configuration/directory_image_pack_library.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>
#include <set>

namespace nookbit {
namespace {
// 유니코드 파일 경로를 백엔드 자료형으로 변환하는 함수.
std::filesystem::path path(const QString& value) { return std::filesystem::path(value.toStdWString()); }
// 팩 보관 오류의 원인을 반환하는 함수.
Error problem(const QString& message) { return {"imagepack.library", message.toStdString()}; }
// 보관소 키가 UUID 형태인지 검사하는 함수.
bool validKey(const QString& key) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return pattern.match(key).hasMatch();
}
// 팩 메타데이터로 선택 목록 항목을 만드는 함수.
ImagePackEntry entry(const std::string& key, const ImagePack& pack) { return {key, pack.name, pack.version}; }
}
// 외부에서 제공한 보관 경로와 로더를 저장하는 생성자.
DirectoryImagePackLibrary::DirectoryImagePackLibrary(const IImagePackLoader& loader, QString bundledManifest, QString storageRoot,
    std::map<std::string, QString> additionalBundled)
    : loader_(loader), bundledManifest_(std::move(bundledManifest)), storageRoot_(std::move(storageRoot)), additionalBundled_(std::move(additionalBundled)) {}
// 내장 팩 및 UUID 폴더의 검증 성공 항목만 목록에 추가하는 함수.
Result<std::vector<ImagePackEntry>> DirectoryImagePackLibrary::list() const {
    std::vector<ImagePackEntry> items;
    auto bundled = load("builtin");
    if (bundled) items.push_back(entry("builtin", bundled.value()));
    for (const auto& [key, manifest] : additionalBundled_) {
        if (!key.starts_with("builtin-") || key == "builtin") continue;
        auto pack = load(key);
        if (pack) items.push_back(entry(key, pack.value()));
    }
    const QDir root(storageRoot_);
    for (const auto& key : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!validKey(key)) continue;
        auto pack = load(key.toStdString());
        if (pack) items.push_back(entry(key.toStdString(), pack.value()));
    }
    return Result<std::vector<ImagePackEntry>>::success(std::move(items));
}
// 목록 키만 경로로 해석하고 심볼릭 링크로 보관소 밖에 나가는 선택을 거부하는 함수.
Result<ImagePack> DirectoryImagePackLibrary::load(const std::string& key) const {
    if (key == "builtin") return loader_.load(path(bundledManifest_));
    if (key.starts_with("builtin-")) {
        const auto found = additionalBundled_.find(key);
        if (found != additionalBundled_.end()) return loader_.load(path(found->second));
    }
    const auto text = QString::fromStdString(key);
    if (!validKey(text)) return Result<ImagePack>::failure(problem(QStringLiteral("알 수 없는 캐릭터 팩입니다.")));
    const QDir root(storageRoot_);
    const auto folder = QFileInfo(root.filePath(text)).canonicalFilePath();
    if (folder.isEmpty() || QFileInfo(folder).dir().canonicalPath() != root.canonicalPath())
        return Result<ImagePack>::failure(problem(QStringLiteral("보관된 캐릭터 팩 폴더를 찾을 수 없습니다.")));
    const QFileInfo manifest(QDir(folder).filePath("imagepack.json"));
    if (QFileInfo(manifest.canonicalFilePath()).canonicalPath() != folder)
        return Result<ImagePack>::failure(problem(QStringLiteral("팩 문서가 보관된 폴더 밖에 있습니다.")));
    return loader_.load(path(manifest.absoluteFilePath()));
}
// 원본은 읽기만 하고 소유한 임시 폴더를 검증한 후 이름 변경으로 공개하는 함수.
Result<ImagePackEntry> DirectoryImagePackLibrary::importFolder(const std::filesystem::path& folder) {
    const auto sourceRoot = QFileInfo(QString::fromStdWString(folder.wstring())).canonicalFilePath();
    if (!folder.is_absolute() || !QFileInfo(sourceRoot).isDir())
        return Result<ImagePackEntry>::failure(problem(QStringLiteral("ImagePack 폴더를 선택해 주세요.")));
    const auto manifest = QDir(sourceRoot).filePath("imagepack.json");
    const auto initial = loader_.load(path(manifest));
    if (!initial) return Result<ImagePackEntry>::failure(initial.error());
    if (!QFileInfo(storageRoot_).isAbsolute() || !QDir().mkpath(storageRoot_))
        return Result<ImagePackEntry>::failure(problem(QStringLiteral("팩 보관 폴더를 만들 수 없습니다.")));
    QTemporaryDir staging(QDir(storageRoot_).filePath(".import-XXXXXX"));
    if (!staging.isValid() || !QFile::copy(manifest, staging.filePath("imagepack.json")))
        return Result<ImagePackEntry>::failure(problem(QStringLiteral("팩 문서를 복사할 수 없습니다.")));
    QFile snapshot(staging.filePath("imagepack.json"));
    if (!snapshot.open(QIODevice::ReadOnly)) return Result<ImagePackEntry>::failure(problem(snapshot.errorString()));
    const auto document = QJsonDocument::fromJson(snapshot.readAll());
    snapshot.close();
    std::set<QString> copied;
    for (const auto& source : document.object().value("sources").toArray()) {
        const auto relative = source.toObject().value("file").toString();
        const auto actual = QFileInfo(QDir(sourceRoot).filePath(relative)).canonicalFilePath();
#ifdef _WIN32
        constexpr auto pathCase = Qt::CaseInsensitive;
#else
        constexpr auto pathCase = Qt::CaseSensitive;
#endif
        if (relative.isEmpty() || relative.contains(QChar::Null) || relative.contains(':') ||
            relative.contains('\\') || relative.startsWith('/') || relative.split('/').contains("..") ||
            !actual.startsWith(sourceRoot + '/', pathCase))
            return Result<ImagePackEntry>::failure(problem(QStringLiteral("팩 밖의 이미지 경로는 가져올 수 없습니다.")));
        if (!copied.insert(relative).second) continue;
        const auto destination = staging.filePath(relative);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(actual, destination))
            return Result<ImagePackEntry>::failure(problem(QStringLiteral("이미지 복사 실패: %1").arg(relative)));
    }
    const auto validated = loader_.load(path(staging.filePath("imagepack.json")));
    if (!validated) return Result<ImagePackEntry>::failure(validated.error());
    const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().rename(staging.path(), QDir(storageRoot_).filePath(key)))
        return Result<ImagePackEntry>::failure(problem(QStringLiteral("가져온 팩을 보관할 수 없습니다.")));
    staging.setAutoRemove(false);
    return Result<ImagePackEntry>::success(entry(key.toStdString(), validated.value()));
}
}
