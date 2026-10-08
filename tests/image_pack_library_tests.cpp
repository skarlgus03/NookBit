#include "infrastructure/configuration/directory_image_pack_library.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "presentation/character_settings_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
// 기대값과 다른 동작을 검사 실패로 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// Qt 파일 경로를 백엔드 경로로 변환하는 함수.
std::filesystem::path path(const QString& text) { return std::filesystem::path(text.toStdWString()); }
// 테스트가 소유한 파일만 작성하는 함수.
void write(const QString& name, const QByteArray& bytes) {
    QFile file(name);
    expect(file.open(QIODevice::WriteOnly), "owned file opens");
    expect(file.write(bytes) == bytes.size(), "owned file writes");
}
// 비동기 가져오기 결과를 제한 시간 안에 기다리는 함수.
void finish(nookbit::BackgroundTaskRunner& runner) {
    QElapsedTimer timer; timer.start();
    while (runner.busy() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    expect(!runner.busy(), "import finishes");
}
/// 복사본 재검증 실패를 주입해 임시 폴더 정리를 검사하는 테스트 로더.
class RejectSnapshot final : public nookbit::IImagePackLoader {
public:
    /// 실제 로더를 주입하는 생성자.
    explicit RejectSnapshot(const nookbit::IImagePackLoader& loader) : loader_(loader) {}
    /// 원본 검증은 통과시키고 임시 복사본만 실패시키는 함수.
    nookbit::Result<nookbit::ImagePack> load(const std::filesystem::path& manifest) const override {
        if (QString::fromStdWString(manifest.wstring()).contains(".import-"))
            return nookbit::Result<nookbit::ImagePack>::failure({"test.snapshot", "Injected snapshot validation failure"});
        return loader_.load(manifest);
    }
private:
    const nookbit::IImagePackLoader& loader_;
};
}
// 실제 팩 복사·실패 보존·배율 범위와 비동기 설정 연결을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    expect(temporary.isValid(), "test storage exists");
    const auto project = QString::fromUtf8(NOOKBIT_SOURCE_DIR);
    const auto bundled = project + "/resources/imagepacks/test/imagepack.json";
    nookbit::JsonImagePackLoader loader(project + "/schemas/imagepack.schema.json");
    const auto source = temporary.filePath(QStringLiteral("원본 팩"));
    QDir().mkpath(source + "/images");
    expect(QFile::copy(bundled, source + "/imagepack.json"), "source manifest copied");
    expect(QFile::copy(project + "/resources/imagepacks/test/images/test-character.png", source + "/images/test-character.png"), "source PNG copied");
    write(source + "/unrelated.txt", "not an asset");
    const auto root = temporary.filePath("library");
    nookbit::DirectoryImagePackLibrary library(loader, bundled, root);
    expect(library.list().value().size() == 1, "builtin available without storage creation");
    expect(!QDir(root).exists(), "listing is read only");
    auto imported = library.importFolder(path(source));
    expect(static_cast<bool>(imported), "valid Unicode folder imports");
    if (!imported) return 1;
    const auto key = QString::fromStdString(imported.value().key);
    auto pack = library.load(imported.value().key);
    expect(static_cast<bool>(pack), "owned copy reloads");
    expect(!QFileInfo::exists(root + '/' + key + "/unrelated.txt"), "unreferenced files not copied");
    expect(QFileInfo::exists(source + "/images/test-character.png"), "original preserved");
    auto duplicate = library.importFolder(path(source));
    expect(duplicate && duplicate.value().key != imported.value().key, "reimport does not overwrite");
    expect(!library.load("../imagepack.json"), "arbitrary selection path refused");
    RejectSnapshot rejecting(loader);
    const auto rejectedRoot = temporary.filePath("rejected");
    nookbit::DirectoryImagePackLibrary failing(rejecting, bundled, rejectedRoot);
    expect(!failing.importFolder(path(source)), "snapshot failure not published");
    expect(QDir(rejectedRoot).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(), "failed snapshot cleaned");
    write(source + "/imagepack.json", "{}");
    expect(!library.importFolder(path(source)), "invalid import rejected");
    expect(library.list().value().size() == 3, "failed import not listed");
    expect(QDir(root).entryList({".import-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(), "no staging residue");
    nookbit::BackgroundTaskRunner runner;
    std::string appliedId;
    nookbit::CharacterSettingsController controller(library, [&](nookbit::ImagePack value) {
        // 적용 창구에서 팩 식별자만 받아 연결을 검사하는 함수.
        appliedId = value.id; return nookbit::Result<void>::success();
    }, runner);
    controller.refresh();
    expect(controller.selectPack(key), "stored pack selects");
    expect(appliedId == "nookbit.test", "apply receives resolved value");
    expect(!controller.selectPack("missing") && controller.selectedKey() == key, "failed selection preserves old pack");
    controller.setUserScale(1.25);
    controller.setUserScale(std::numeric_limits<double>::quiet_NaN());
    controller.setUserScale(0);
    expect(controller.userScale() == 1.25, "invalid scales ignored");
    controller.importFolder(QUrl("https://example.invalid/pack"));
    expect(!controller.busy() && controller.selectedKey() == key, "remote import refused");
    controller.importFolder(QUrl::fromLocalFile(source));
    finish(runner);
    expect(controller.selectedKey() == key, "invalid async import preserves selection");
    controller.importFolder(QUrl::fromLocalFile(project + "/resources/imagepacks/test"));
    expect(controller.busy(), "valid import dispatched asynchronously");
    finish(runner);
    expect(controller.selectedKey() != key && library.list().value().size() == 4, "successful async import selects owned pack");
    nookbit::DirectoryImagePackLibrary reopened(loader, bundled, root);
    expect(static_cast<bool>(reopened.load(controller.selectedKey().toStdString())), "saved key resolves after reopening library");
    std::cout << "ImagePack library failures: " << failures << '\n';
    return failures ? 1 : 0;
}
