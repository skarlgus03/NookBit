#include "presentation/user_guide_controller.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "modules/rules/rule_engine.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <iostream>

// 내장 사용자 문서의 저장·오류와 실제로 읽히는 최소 캐릭터팩 예제를 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    nookbit::UserGuideController guides;
    for (const auto& kind : {QStringLiteral("rules"), QStringLiteral("character")}) {
        const auto text = guides.text(kind);
        if (text.isEmpty() || !text.startsWith("# ") || !text.contains("Standalone authoring contract")) return 1;
        if (QRegularExpression(QStringLiteral("[\\x{1100}-\\x{11FF}\\x{3130}-\\x{318F}\\x{AC00}-\\x{D7AF}]")).match(text).hasMatch()) return 1;
        const auto destination = directory.filePath(kind + ".md");
        if (!guides.save(kind, QUrl::fromLocalFile(destination))) return 1;
        QFile saved(destination);
        if (!saved.open(QIODevice::ReadOnly) || saved.readAll() != text.toUtf8()) return 1;
    }
    if (!guides.text("../../README").isEmpty() || guides.save("rules", QUrl("https://example.invalid/guide.md")) || guides.error().isEmpty()) return 1;
    guides.clearError();
    if (!guides.error().isEmpty()) return 1;
    if (guides.suggestedFile("rules").fileName() != "NookBit-Rules-AI.md" ||
        guides.suggestedFile("character").fileName() != "NookBit-CharacterPack-Guide.md") return 1;
    // 규칙 다운로드가 일반 사용 설명이 아닌 독립적인 AI 작성 계약인지 확인한다.
    const auto rules = guides.text("rules");
    if (!rules.contains("Standalone authoring contract") || rules.contains("](../")) return 1;
    int ruleCursor = 0, ruleExampleCount = 0;
    bool checkedFolderExample = false;
    while ((ruleCursor = rules.indexOf("```json\n", ruleCursor)) >= 0) {
        const auto ruleBegin = ruleCursor + 8;
        const auto ruleEnd = rules.indexOf("```", ruleBegin);
        if (ruleEnd < ruleBegin) return 1;
        QFile ruleFile(directory.filePath(QStringLiteral("rules-%1.json").arg(++ruleExampleCount)));
        if (!ruleFile.open(QIODevice::WriteOnly)) return 1;
        ruleFile.write(rules.mid(ruleBegin, ruleEnd - ruleBegin).toUtf8()); ruleFile.close();
        nookbit::JsonRuleSetRepository ruleRepository(ruleFile.fileName(), QStringLiteral(":/nookbit/rules-tree-v3.schema.json"));
        const auto loaded = ruleRepository.load();
        if (!loaded) { std::cerr << loaded.error().message; return 1; }
        if (loaded.value().id == "folders-only") {
            nookbit::RuleEngine engine;
            nookbit::FileFact target; target.relativePath = L"same-name.png"; target.fingerprint.directory = true;
            const auto folder = engine.decide(target, loaded.value(), nookbit::MultiMatchPolicy::FirstMatch, 0);
            target.fingerprint.directory = false;
            const auto file = engine.decide(target, loaded.value(), nookbit::MultiMatchPolicy::FirstMatch, 0);
            if (!folder || !folder.value() || folder.value()->destinationRelativePath != L"Folders" || !file || file.value()) return 1;
            checkedFolderExample = true;
        }
        ruleCursor = ruleEnd + 3;
    }
    if (ruleExampleCount < 2 || !checkedFolderExample) return 1;
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
    // 문서의 부분 JSON 예제를 최소 팩에 병합해 아틀라스와 말풍선도 검사한다.
    auto root = QJsonDocument::fromJson(json).object();
    auto next = end + 3;
    for (int variant = 0; variant < 2; ++variant) {
        const auto fragmentStart = character.indexOf("```json\n", next);
        if (fragmentStart < 0) return 1;
        const auto fragmentEnd = character.indexOf("```", fragmentStart + 8);
        if (fragmentEnd < 0) return 1;
        const auto fragment = QJsonDocument::fromJson(("{" + character.mid(fragmentStart + 8, fragmentEnd - fragmentStart - 8) + "}").toUtf8());
        if (!fragment.isObject()) return 1;
        const auto fields = fragment.object();
        for (auto it = fields.begin(); it != fields.end(); ++it) root.insert(it.key(), it.value());
        if (variant == 0) {
            QImage sheet(256, 128, QImage::Format_ARGB32);
            sheet.fill(Qt::transparent);
            if (!sheet.save(directory.filePath("images/sheet.png"))) return 1;
        } else {
            QImage bubble(270, 120, QImage::Format_ARGB32);
            bubble.fill(Qt::black);
            if (!bubble.save(directory.filePath("images/bubble.png"))) return 1;
            auto sources = root.value("sources").toArray();
            sources.append(QJsonObject{{"id", "bubble-bg"}, {"kind", "image"}, {"file", "images/bubble.png"}});
            root.insert("sources", sources);
        }
        if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
        manifest.write(QJsonDocument(root).toJson()); manifest.close();
        const auto checked = loader.load(std::filesystem::path(manifest.fileName().toStdWString()));
        if (!checked) { std::cerr << checked.error().message; return 1; }
        next = fragmentEnd + 3;
    }
    std::cout << "Download documents, rule JSON, individual PNG, atlas and bubble examples passed\n";
    return 0;
}
