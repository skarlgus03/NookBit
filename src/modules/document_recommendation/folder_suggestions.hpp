#pragma once

#include "modules/document_recommendation/tfidf.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringConverter>

#ifdef NOOKBIT_DOCUMENT_PDF
#include <QPdfDocument>
#include <QPdfSelection>
#endif

namespace nookbit::document_recommendation {

struct FolderSuggestions {
    QMap<QString, QString> destinations;
    QSet<QString> unreadablePdfs;
    int candidateCount{0};
};

// 문서 추천에서 검사하는 확장자인지 확인하는 함수.
inline bool isDocumentExtension(const QString& extension) {
    return extension == "txt" || extension == "md" || extension == "pdf";
}

#ifdef NOOKBIT_DOCUMENT_PDF
// PDF 앞쪽 페이지의 텍스트를 지정한 길이까지 읽는 함수.
inline QString pdfExcerpt(const QString& path, qsizetype characters) {
    QPdfDocument document(nullptr);
    if (document.load(path) != QPdfDocument::Error::None) return {};
    QString text;
    const int pages = std::min(document.pageCount(), 5);
    for (int page = 0; page < pages && text.size() < characters; ++page) {
        const auto pageText = document.getAllText(page).text().trimmed();
        if (pageText.isEmpty()) continue;
        if (!text.isEmpty()) text.append('\n');
        text.append(pageText.left(characters - text.size()));
    }
    return text;
}
#endif

// 연결 경로를 제외하고 로컬 문서의 본문 앞부분을 가져오는 함수.
inline QString documentExcerpt(const QFileInfo& info, qsizetype characters) {
    const auto extension = info.suffix().toLower();
    if (!info.isFile() || info.isSymLink() || !isDocumentExtension(extension)) return {};
    if (extension == "pdf") {
#ifdef NOOKBIT_DOCUMENT_PDF
        return pdfExcerpt(info.absoluteFilePath(), characters);
#else
        return {};
#endif
    }
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

// 사용자가 체크할 수 있는 바로 아래 폴더 이름을 가져오는 함수.
inline QFileInfoList candidateFolders(const QString& root) {
    if (root.isEmpty()) return {};
    auto folders = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    folders.removeIf([](const QFileInfo& folder) { return folder.isJunction(); });
    return folders;
}

// 후보 폴더의 대표 문서를 읽고 이동 계획과 별개로 추천하는 함수.
inline FolderSuggestions suggestFolders(const QString& homePath, const QStringList& documents,
                                        const QStringList& previewSources,
                                        const std::optional<QStringList>& selectedFolders = std::nullopt) {
    constexpr int candidateLimit = 32;
    constexpr double minimumSimilarity = 0.286;
    QSet<QString> excluded;
    for (const auto& path : previewSources) excluded.insert(documentKey(QFileInfo(path)));
    std::vector<Profile> profiles;
    const auto folders = candidateFolders(homePath);
    for (const auto& folder : folders) {
        if (selectedFolders && !selectedFolders->contains(folder.fileName())) continue;
        const QDir directory(folder.absoluteFilePath());
        QStringList examples;
        const auto files = directory.entryInfoList({QStringLiteral("*.txt"), QStringLiteral("*.md"), QStringLiteral("*.pdf")},
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
    const auto idf = prepareProfiles(profiles);
    for (const auto& source : documents) {
        const QFileInfo file(source);
        const auto text = documentExcerpt(file, 4000);
        if (text.isEmpty()) {
            if (file.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0)
                result.unreadablePdfs.insert(source);
            continue;
        }
        if (profiles.empty()) continue;
        const auto prediction = recommend(file.fileName() + '\n' + text, profiles, idf);
        if (prediction.bestScore && *prediction.bestScore >= minimumSimilarity)
            result.destinations.insert(source, prediction.ranked.front());
    }
    return result;
}

} // namespace nookbit::document_recommendation
