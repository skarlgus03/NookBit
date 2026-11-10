#pragma once

#include "modules/document_recommendation/tfidf.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringConverter>

namespace nookbit::document_recommendation {

struct FolderSuggestions {
    QMap<QString, QString> destinations;
    int candidateCount{0};
};

// 연결 경로를 제외하고 읽을 수 있는 UTF-8 문서 앞부분을 가져오는 함수.
inline QString documentExcerpt(const QFileInfo& info, qsizetype characters) {
    const auto extension = info.suffix().toLower();
    if (!info.isFile() || info.isSymLink() || (extension != "txt" && extension != "md")) return {};
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    QStringDecoder decode(QStringDecoder::Utf8);
    const QString text = decode(file.read(characters * 4));
    return file.error() == QFileDevice::NoError && !decode.hasError() ? text.left(characters).trimmed() : QString{};
}

// 대기 문서가 자기 자신의 예시가 되지 않도록 정규 경로를 비교하는 함수.
inline QString documentKey(const QFileInfo& info) {
    const auto path = info.canonicalFilePath();
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

// 정리 위치 바로 아래의 폴더에서 대표 문서를 읽고 이동 계획과 별개로 추천하는 함수.
inline FolderSuggestions suggestFolders(const QString& homePath, const QStringList& documents,
                                        const QStringList& previewSources) {
    constexpr int candidateLimit = 32;
    constexpr double minimumSimilarity = 0.286;
    const QDir home(homePath);
    QSet<QString> excluded;
    for (const auto& path : previewSources) excluded.insert(documentKey(QFileInfo(path)));
    std::vector<Profile> profiles;
    const auto folders = home.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const auto& folder : folders) {
        if (folder.isJunction()) continue;
        const QDir directory(folder.absoluteFilePath());
        QStringList examples;
        const auto files = directory.entryInfoList({QStringLiteral("*.txt"), QStringLiteral("*.md")},
            QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const auto& file : files) {
            if (excluded.contains(documentKey(file))) continue;
            const auto text = documentExcerpt(file, 1200);
            if (!text.isEmpty() && !examples.contains(text)) examples.append(text);
            if (examples.size() == 2) break;
        }
        if (examples.isEmpty()) continue;
        profiles.push_back({folder.absoluteFilePath(), folder.fileName() + '\n' + examples.join('\n'), {}});
        if (profiles.size() == candidateLimit) break;
    }

    FolderSuggestions result;
    result.candidateCount = static_cast<int>(profiles.size());
    if (profiles.empty()) return result;
    const auto idf = prepareProfiles(profiles);
    for (const auto& source : documents) {
        const QFileInfo file(source);
        const auto text = documentExcerpt(file, 4000);
        if (text.isEmpty()) continue;
        const auto prediction = recommend(file.fileName() + '\n' + text, profiles, idf);
        if (prediction.bestScore && *prediction.bestScore >= minimumSimilarity)
            result.destinations.insert(source, prediction.ranked.front());
    }
    return result;
}

} // namespace nookbit::document_recommendation
