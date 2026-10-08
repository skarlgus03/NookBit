#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <cmath>
#include <utility>

namespace nookbit {
namespace {
struct Source {
    QString path;
    bool atlas{false}, grid{false};
    int width{0}, height{0}, frameWidth{0}, frameHeight{0}, columns{0}, rows{0};
};
// 원인이 발생한 항목 이름을 오류에 함께 남기는 함수.
Error problem(const char* code, const std::string& id, const std::string& detail) {
    return {std::string("imagepack.") + code, id + ": " + detail};
}
// JSON의 논리 좌표를 Qt 독립 자료형으로 읽는 함수.
ImagePackPoint point(const QJsonObject& object) {
    return {object.value("x").toDouble(), object.value("y").toDouble()};
}
// 팩 내부 PNG의 실제 경로·형식·헤더 크기와 격자를 검증하는 함수.
Result<Source> readSource(const QJsonObject& object, const QString& root, const std::string& id) {
    const auto relative = object.value("file").toString();
    if (relative.contains(QChar::Null) || relative.contains(':') || relative.contains('\\') ||
        relative.startsWith('/') || relative.split('/').contains(QStringLiteral("..")) ||
        QFileInfo(relative).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0)
        return Result<Source>::failure(problem("asset_path", id, "Use a relative PNG path inside the pack."));
    const QFileInfo info(QDir(root).filePath(relative));
    const auto canonical = info.canonicalFilePath();
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    const auto prefix = root.endsWith('/') ? root : root + '/';
    if (!info.isFile() || canonical.isEmpty())
        return Result<Source>::failure(problem("asset_missing", id, "PNG file not found: " + relative.toStdString()));
    if (!canonical.startsWith(prefix, pathCase))
        return Result<Source>::failure(problem("asset_path", id, "Resolved image is outside the pack directory."));
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly))
        return Result<Source>::failure(problem("asset_unreadable", id, file.errorString().toStdString()));
    QImageReader reader(&file);
    if (!reader.canRead() || reader.format() != "png")
        return Result<Source>::failure(problem("asset_format", id, "Expected a readable PNG header: " + relative.toStdString()));
    const auto size = reader.size();
    if (size.width() <= 0 || size.height() <= 0)
        return Result<Source>::failure(problem("asset_size", id, "Invalid PNG dimensions."));
    Source source;
    source.path = canonical; source.width = size.width(); source.height = size.height();
    source.atlas = object.value("kind").toString() == "atlas";
    source.grid = object.contains("grid");
    if (source.grid) {
        const auto grid = object.value("grid").toObject();
        source.frameWidth = grid.value("frameWidth").toInt(-1);
        source.frameHeight = grid.value("frameHeight").toInt(-1);
        source.columns = grid.value("columns").toInt(-1);
        source.rows = grid.value("rows").toInt(-1);
        if (source.frameWidth < 1 || source.frameHeight < 1 || source.columns < 1 || source.rows < 1 ||
            static_cast<qint64>(source.frameWidth) * source.columns != source.width ||
            static_cast<qint64>(source.frameHeight) * source.rows != source.height)
            return Result<Source>::failure(problem("grid_size", id, "Grid does not match the PNG dimensions."));
    }
    return Result<Source>::success(std::move(source));
}
// 이미지 전체·격자 번호·직사각형을 동일한 원본 픽셀 영역으로 확정하는 함수.
Result<ImagePackRect> resolveRegion(const QJsonObject& region, const Source& source, const std::string& id) {
    const auto type = region.value("type").toString();
    ImagePackRect result;
    if (type == "image" && !source.atlas) result = {0, 0, source.width, source.height};
    else if (type == "cell" && source.atlas && source.grid) {
        const auto index = region.value("index").toInt(-1);
        if (index < 0 || index >= static_cast<qint64>(source.columns) * source.rows)
            return Result<ImagePackRect>::failure(problem("cell_range", id, "Cell index is outside the atlas grid."));
        result = {(index % source.columns) * source.frameWidth,
                  (index / source.columns) * source.frameHeight, source.frameWidth, source.frameHeight};
    } else if (type == "rect" && source.atlas) {
        result = {region.value("x").toInt(-1), region.value("y").toInt(-1),
                  region.value("width").toInt(-1), region.value("height").toInt(-1)};
    } else return Result<ImagePackRect>::failure(problem("region_source", id, "Region kind is incompatible with its source."));
    if (result.x < 0 || result.y < 0 || result.width < 1 || result.height < 1 ||
        static_cast<qint64>(result.x) + result.width > source.width ||
        static_cast<qint64>(result.y) + result.height > source.height)
        return Result<ImagePackRect>::failure(problem("rect_range", id, "Frame rectangle is outside the PNG."));
    return Result<ImagePackRect>::success(result);
}
} // namespace

// 검증용 Schema 위치를 보관하는 생성자.
JsonImagePackLoader::JsonImagePackLoader(QString schemaPath) : schemaPath_(std::move(schemaPath)) {}

// 매니페스트와 이미지 크기·참조를 검증해 팩을 반환하는 함수.
Result<ImagePack> JsonImagePackLoader::load(const std::filesystem::path& manifestPath) const {
    const QFileInfo manifest(QString::fromStdWString(manifestPath.wstring()));
    if (!manifest.isAbsolute() || !manifest.isFile())
        return Result<ImagePack>::failure(problem("manifest_path", "manifest", "Use an existing absolute manifest file path."));
    const auto loaded = loadValidatedJson(manifest.canonicalFilePath(), schemaPath_);
    if (!loaded) return Result<ImagePack>::failure(loaded.error());
    const auto& object = loaded.value();
    const auto root = QFileInfo(manifest.canonicalFilePath()).canonicalPath();
    ImagePack pack;
    pack.id = object.value("id").toString().toStdString();
    pack.name = object.value("name").toString().toStdString();
    pack.version = object.value("version").toString().toStdString();
    pack.author = object.value("author").toString().toStdString();
    const auto display = object.value("display").toObject();
    const auto size = display.value("referenceSize").toObject();
    pack.referenceSize = {size.value("width").toDouble(), size.value("height").toDouble()};
    pack.filtering = display.value("filtering").toString() == "linear" ? ImagePackFiltering::Linear : ImagePackFiltering::Nearest;
    const auto defaultPivot = point(display.value("pivot").toObject());
    const auto defaultRatio = display.value("pixelsPerUnit").toDouble();
    std::map<std::string, Source> sources;
    for (const auto& value : object.value("sources").toArray()) {
        const auto sourceObject = value.toObject();
        const auto id = sourceObject.value("id").toString().toStdString();
        if (sources.contains(id)) return Result<ImagePack>::failure(problem("duplicate_source", id, "Duplicate source ID."));
        auto source = readSource(sourceObject, root, id);
        if (!source) return Result<ImagePack>::failure(source.error());
        sources.emplace(id, std::move(source.value()));
    }
    for (const auto& value : object.value("frames").toArray()) {
        const auto frameObject = value.toObject();
        const auto id = frameObject.value("id").toString().toStdString();
        if (pack.frames.contains(id)) return Result<ImagePack>::failure(problem("duplicate_frame", id, "Duplicate frame ID."));
        const auto source = sources.find(frameObject.value("sourceId").toString().toStdString());
        if (source == sources.end()) return Result<ImagePack>::failure(problem("unknown_source", id, "Source ID not found."));
        const auto region = resolveRegion(frameObject.value("region").toObject(), source->second, id);
        if (!region) return Result<ImagePack>::failure(region.error());
        const auto ratio = frameObject.contains("pixelsPerUnit") ? frameObject.value("pixelsPerUnit").toDouble() : defaultRatio;
        if (!std::isfinite(region.value().width / ratio) || !std::isfinite(region.value().height / ratio))
            return Result<ImagePack>::failure(problem("logical_size", id, "Pixel density produces nonfinite logical dimensions."));
        pack.frames.emplace(id, ImagePackFrame{std::filesystem::path(source->second.path.toStdWString()),
            source->second.width, source->second.height, region.value(),
            frameObject.contains("pivot") ? point(frameObject.value("pivot").toObject()) : defaultPivot, ratio});
    }
    for (const auto& value : object.value("animations").toArray()) {
        const auto animationObject = value.toObject();
        const auto id = animationObject.value("id").toString().toStdString();
        if (pack.animations.contains(id)) return Result<ImagePack>::failure(problem("duplicate_animation", id, "Duplicate animation ID."));
        ImagePackAnimation animation;
        animation.loop = animationObject.value("loop").toBool();
        for (const auto& valueStep : animationObject.value("steps").toArray()) {
            const auto step = valueStep.toObject();
            const auto frameId = step.value("frameId").toString().toStdString();
            if (!pack.frames.contains(frameId)) return Result<ImagePack>::failure(problem("unknown_frame", id, "Frame ID not found: " + frameId));
            animation.steps.push_back({frameId, step.value("durationMs").toInt()});
        }
        pack.animations.emplace(id, std::move(animation));
    }
    for (const auto& value : object.value("bindings").toArray()) {
        const auto binding = value.toObject();
        const auto cue = binding.value("cueId").toString().toStdString();
        const auto animation = binding.value("animationId").toString().toStdString();
        if (pack.bindings.contains(cue)) return Result<ImagePack>::failure(problem("duplicate_binding", cue, "Duplicate cue binding."));
        if (!pack.animations.contains(animation)) return Result<ImagePack>::failure(problem("unknown_animation", cue, "Animation ID not found: " + animation));
        pack.bindings.emplace(cue, animation);
    }
    for (const auto* cue : {"resting", "calling", "working"}) {
        if (!pack.bindings.contains(cue)) return Result<ImagePack>::failure(problem("fallback_missing", cue, "Required fallback binding is missing."));
    }
    return Result<ImagePack>::success(std::move(pack));
}
} // namespace nookbit
