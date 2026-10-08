#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <iostream>

namespace {
int failures = 0;
// 조건이 다르면 검사 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 프로젝트 예제·스키마의 절대 경로를 만드는 함수.
QString projectPath(const char* relative) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/" + QString::fromUtf8(relative);
}
struct Fixture {
    QTemporaryDir directory;
    QJsonObject original;
    nookbit::JsonImagePackLoader loader{projectPath("schemas/imagepack.schema.json")};
    // 고유 임시 폴더에 테스트 PNG와 매니페스트 예제를 준비하는 생성자.
    explicit Fixture(bool atlas) {
        expect(directory.isValid(), "fixture directory must exist");
        if (!directory.isValid()) return;
        QDir().mkpath(directory.filePath("images"));
        QFile input(projectPath(atlas ? "resources/imagepacks/examples/atlas.imagepack.json"
            : "resources/imagepacks/examples/individual.imagepack.json"));
        expect(input.open(QIODevice::ReadOnly), "example manifest must open");
        if (!input.isOpen()) return;
        original = QJsonDocument::fromJson(input.readAll()).object();
        if (atlas) {
            expect(QFile::copy(projectPath("resources/character/sprites/test-character.png"),
                directory.filePath("images/test-character.png")), "owned atlas copy must be created");
        } else {
            for (const auto* name : {"idle-a", "idle-b", "wave", "carry"}) {
                const bool trimmed = QString::fromUtf8(name) == "carry";
                QImage image(trimmed ? 384 : 512, trimmed ? 448 : 512, QImage::Format_ARGB32);
                image.fill(Qt::transparent);
                expect(image.save(directory.filePath(QStringLiteral("images/%1.png").arg(name)), "PNG"),
                    "owned individual PNG must be created");
            }
        }
    }
    // 테스트 소유 매니페스트를 저장하는 함수.
    void write(const QJsonObject& object) {
        QFile file(directory.filePath("imagepack.json"));
        expect(file.open(QIODevice::WriteOnly), "owned manifest must open");
        if (file.isOpen()) {
            const auto bytes = QJsonDocument(object).toJson();
            expect(file.write(bytes) == bytes.size(), "owned manifest must be fully written");
        }
    }
    // 공통 인터페이스를 통해 저장한 팩을 읽는 함수.
    nookbit::Result<nookbit::ImagePack> load() {
        const nookbit::IImagePackLoader& port = loader;
        return port.load(std::filesystem::path(directory.filePath("imagepack.json").toStdWString()));
    }
};
// 배열의 지정 항목을 수정한 사본으로 교체하는 함수.
void edit(QJsonObject& object, const char* key, int index, const std::function<void(QJsonObject&)>& change) {
    auto entries = object.value(key).toArray();
    auto entry = entries[index].toObject();
    change(entry); entries[index] = entry; object[key] = entries;
}
// 수정한 매니페스트가 항목을 식별하는 오류로 거절되는지 검사하는 함수.
void fails(Fixture& fixture, const std::function<void(QJsonObject&)>& change,
    const char* code, const char* detail) {
    auto object = fixture.original;
    change(object); fixture.write(object);
    const auto result = fixture.load();
    expect(!result, "invalid pack must be rejected");
    if (!result) {
        expect(result.error().code == code, "failure must have the expected code");
        expect(result.error().message.find(detail) != std::string::npos, "failure must identify the relevant item or field");
    }
}
// 개별 이미지·격자·직사각형이 공통 프레임과 올바른 상속 값으로 변환되는지 검사하는 함수.
void testResolvedFrames() {
    Fixture individual(false);
    individual.write(individual.original);
    const auto png = individual.load();
    expect(png.hasValue(), "individual PNG example with real assets must load");
    if (png) {
        const auto& first = png.value().frames.at("idle-a");
        const auto& trimmed = png.value().frames.at("carry");
        expect(first.region.x == 0 && first.region.width == 512 && first.region.height == 512 &&
            first.pivot.x == 64 && first.pivot.y == 112 && first.pixelsPerUnit == 4 && first.imagePath.is_absolute(),
            "individual frames must resolve the full PNG and inherit logical pivot and ratio");
        expect(trimmed.region.width == 384 && trimmed.region.height == 448 && trimmed.pivot.x == 32 && trimmed.pivot.y == 96,
            "cropped PNG frames must retain their explicit logical pivot without normalization");
        expect(png.value().referenceSize.width == 128 && png.value().filtering == nookbit::ImagePackFiltering::Linear &&
            png.value().bindings.at("file.offer") == "call" && png.value().animations.at("idle").steps.size() == 2,
            "metadata, filtering, cue mappings and animation order must be preserved");
    }
    auto custom = individual.original;
    edit(custom, "frames", 0, [](auto& frame) { frame["pivot"] = QJsonObject{{"x", -10}, {"y", 200}}; frame["pixelsPerUnit"] = 2; });
    individual.write(custom);
    const auto override = individual.load();
    expect(override && override.value().frames.at("idle-a").pivot.x == -10 &&
        override.value().frames.at("idle-a").pivot.y == 200 && override.value().frames.at("idle-a").pixelsPerUnit == 2,
        "per-frame ratio and pivots outside the image must be supported");
    Fixture atlas(true);
    atlas.write(atlas.original);
    const auto grid = atlas.load();
    expect(grid.hasValue(), "atlas example with real assets must load");
    if (grid) {
        const auto& cell = grid.value().frames.at("call-1");
        const auto& rectangle = grid.value().frames.at("work-1");
        expect(cell.region.x == 128 && cell.region.y == 128 && cell.region.width == 128 && cell.pivot.x == 64 && cell.pivot.y == 96,
            "cell indices must resolve to source pixels while pivots remain relative logical coordinates");
        expect(rectangle.region.x == 128 && rectangle.region.y == 256 && rectangle.imageWidth == 512 && rectangle.imageHeight == 384,
            "rectangle frames must resolve to the same source-path and region representation");
    }
    auto packed = atlas.original;
    edit(packed, "sources", 0, [](auto& source) { source.remove("grid"); });
    auto packedFrames = packed.value("frames").toArray();
    for (int index = 0; index < packedFrames.size(); ++index) {
        auto entry = packedFrames[index].toObject();
        const auto region = entry.value("region").toObject();
        if (region.value("type").toString() == "cell") {
            const auto cell = region.value("index").toInt();
            entry["region"] = QJsonObject{{"type", "rect"}, {"x", (cell % 4) * 128},
                {"y", (cell / 4) * 128}, {"width", 128}, {"height", 128}};
            packedFrames[index] = entry;
        }
    }
    packed["frames"] = packedFrames; atlas.write(packed);
    const auto ungridded = atlas.load();
    expect(ungridded && ungridded.value().frames.at("call-1").region.y == 128,
        "atlas rectangles must work without a grid declaration");
    expect(QFile::copy(projectPath("resources/character/sprites/test-character.png"),
        individual.directory.filePath("images/test-character.png")), "mixed-pack atlas should be copied into its owned directory");
    auto mixed = individual.original;
    auto mixedSources = mixed.value("sources").toArray();
    mixedSources.append(atlas.original.value("sources").toArray()[0]); mixed["sources"] = mixedSources;
    auto mixedFrames = mixed.value("frames").toArray();
    mixedFrames.append(QJsonObject{{"id", "atlas-frame"}, {"sourceId", "test-sheet"},
        {"region", QJsonObject{{"type", "cell"}, {"index", 0}}}, {"pixelsPerUnit", 1}});
    mixed["frames"] = mixedFrames; individual.write(mixed);
    const auto combined = individual.load();
    expect(combined && combined.value().frames.at("idle-a").region.width / combined.value().frames.at("idle-a").pixelsPerUnit == 128 &&
        combined.value().frames.at("atlas-frame").region.width / combined.value().frames.at("atlas-frame").pixelsPerUnit == 128 &&
        combined.value().frames.at("atlas-frame").pivot.x == 64,
        "mixed-resolution PNG and atlas sources must share the same logical size and inherited pivot");
    fails(atlas, [](auto& object) { edit(object, "frames", 0, [](auto& frame) { frame["region"] = QJsonObject{{"type", "cell"}, {"index", 12}}; }); },
        "imagepack.cell_range", "idle-0");
    expect(grid && grid.value().frames.at("idle-0").region.width == 128, "failed subsequent loads must not alter prior returned data");
}
// 구조·참조·이미지 영역·경로·숫자의 잘못된 입력을 검사하는 함수.
void testFailures() {
    Fixture fixture(true);
    fails(fixture, [](auto& object) { object["schemaVersion"] = 2; }, "json.schema_invalid", "schemaVersion");
    fails(fixture, [](auto& object) { auto display = object.value("display").toObject(); display.remove("pivot"); object["display"] = display; }, "json.schema_invalid", "pivot");
    fails(fixture, [](auto& object) { auto display = object.value("display").toObject(); display["pixelsPerUnit"] = 0; object["display"] = display; }, "json.schema_invalid", "pixelsPerUnit");
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { source["kind"] = "unknown"; }); }, "json.schema_invalid", "sources");
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { source["file"] = "../outside.png"; }); }, "imagepack.asset_path", "test-sheet");
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { source["file"] = "C:/outside.png"; }); }, "imagepack.asset_path", "test-sheet");
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { source["file"] = "images/missing.png"; }); }, "imagepack.asset_missing", "test-sheet");
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { auto grid = source.value("grid").toObject(); grid["columns"] = 3; source["grid"] = grid; }); }, "imagepack.grid_size", "test-sheet");
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { source.remove("grid"); }); }, "imagepack.region_source", "idle-0");
    fails(fixture, [](auto& object) { edit(object, "frames", 0, [](auto& frame) { frame["region"] = QJsonObject{{"type", "image"}}; }); }, "imagepack.region_source", "idle-0");
    fails(fixture, [](auto& object) { edit(object, "frames", 0, [](auto& frame) { frame["region"] = QJsonObject{{"type", "unknown"}}; }); }, "json.schema_invalid", "region");
    fails(fixture, [](auto& object) { edit(object, "frames", 0, [](auto& frame) { frame["region"] = QJsonObject{{"type", "cell"}, {"index", 3e9}}; }); }, "imagepack.cell_range", "idle-0");
    fails(fixture, [](auto& object) { edit(object, "frames", 5, [](auto& frame) { frame["region"] = QJsonObject{{"type", "rect"}, {"x", 128}, {"y", 256}, {"width", 385}, {"height", 128}}; }); }, "imagepack.rect_range", "work-1");
    fails(fixture, [](auto& object) { auto entries = object.value("sources").toArray(); entries.append(entries[0]); object["sources"] = entries; }, "imagepack.duplicate_source", "test-sheet");
    fails(fixture, [](auto& object) { edit(object, "frames", 1, [](auto& frame) { frame["id"] = "idle-0"; }); }, "imagepack.duplicate_frame", "idle-0");
    fails(fixture, [](auto& object) { edit(object, "frames", 0, [](auto& frame) { frame["sourceId"] = "missing"; }); }, "imagepack.unknown_source", "idle-0");
    fails(fixture, [](auto& object) { edit(object, "animations", 0, [](auto& animation) { animation["steps"] = QJsonArray{QJsonObject{{"frameId", "Idle-0"}, {"durationMs", 100}}}; }); }, "imagepack.unknown_frame", "Idle-0");
    fails(fixture, [](auto& object) { edit(object, "animations", 1, [](auto& animation) { animation["id"] = "idle"; }); }, "imagepack.duplicate_animation", "idle");
    fails(fixture, [](auto& object) { edit(object, "bindings", 0, [](auto& binding) { binding["animationId"] = "missing"; }); }, "imagepack.unknown_animation", "resting");
    fails(fixture, [](auto& object) { auto bindings = object.value("bindings").toArray(); bindings.removeAt(0); object["bindings"] = bindings; }, "imagepack.fallback_missing", "resting");
    fails(fixture, [](auto& object) { auto bindings = object.value("bindings").toArray(); bindings.append(bindings[0]); object["bindings"] = bindings; }, "imagepack.duplicate_binding", "resting");
    QFile invalid(fixture.directory.filePath("images/broken.png"));
    expect(invalid.open(QIODevice::WriteOnly), "owned malformed PNG must open");
    invalid.write("not a PNG"); invalid.close();
    fails(fixture, [](auto& object) { edit(object, "sources", 0, [](auto& source) { source["file"] = "images/broken.png"; }); }, "imagepack.asset_format", "test-sheet");
}
} // namespace
// 이미지를 화면에 표시하지 않고 ImagePack 로더를 검사하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testResolvedFrames(); testFailures();
    return failures == 0 ? 0 : 1;
}
