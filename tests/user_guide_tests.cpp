#include "presentation/user_guide_controller.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <iostream>

// 내장 사용자 문서의 저장·오류와 실제로 읽히는 최소 캐릭터팩 예제를 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    nookbit::UserGuideController guides;
    for (const auto& kind : {QStringLiteral("rules"), QStringLiteral("character")}) {
        const auto text = guides.text(kind);
        if (text.isEmpty() || !text.startsWith("# ")) return 1;
        const auto destination = directory.filePath(kind + ".md");
        if (!guides.save(kind, QUrl::fromLocalFile(destination))) return 1;
        QFile saved(destination);
        if (!saved.open(QIODevice::ReadOnly) || saved.readAll() != text.toUtf8()) return 1;
    }
    if (!guides.text("../../README").isEmpty() || guides.save("rules", QUrl("https://example.invalid/guide.md")) || guides.error().isEmpty()) return 1;
    guides.clearError();
    if (!guides.error().isEmpty()) return 1;
    const auto character = guides.text("character");
    const auto begin = character.indexOf("```json\n") + 8;
    const auto end = character.indexOf("```", begin);
    if (begin < 8 || end < begin) return 1;
    const auto json = character.mid(begin, end - begin).toUtf8();
    if (!QJsonDocument::fromJson(json).isObject()) return 1;
    QDir().mkpath(directory.filePath("images"));
    QImage picture(128, 128, QImage::Format_ARGB32);
    picture.fill(Qt::transparent);
    if (!picture.save(directory.filePath("images/character.png"))) return 1;
    QFile manifest(directory.filePath("imagepack.json"));
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(json) != json.size()) return 1;
    manifest.close();
    nookbit::JsonImagePackLoader loader;
    const auto pack = loader.load(std::filesystem::path(manifest.fileName().toStdWString()));
    if (!pack) { std::cerr << pack.error().message; return 1; }
    std::cout << "Bundled user guides, export and minimal character pack passed\n";
    return 0;
}
