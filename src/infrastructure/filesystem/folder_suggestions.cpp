#include "infrastructure/filesystem/folder_suggestions.hpp"

#include "infrastructure/filesystem/document_text_terms.hpp"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QStringConverter>

#include <algorithm>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif

#ifdef NOOKBIT_DOCUMENT_PDF
#include <QPdfDocument>
#include <QPdfSelection>
#endif

namespace nookbit::document_recommendation {
namespace {

struct Excerpt {
    QString text;
    ExcerptIssue issue{ExcerptIssue::None};
};

#ifdef NOOKBIT_DOCUMENT_PDF
// PDF 앞쪽 페이지의 텍스트를 지정한 길이까지 읽는 함수.
Excerpt pdfExcerpt(const QString& path, qsizetype characters) {
    QPdfDocument document(nullptr);
    if (document.load(path) != QPdfDocument::Error::None) return {{}, ExcerptIssue::Unreadable};
    QString text;
    const int pages = std::min(document.pageCount(), 5);
    for (int page = 0; page < pages && text.size() < characters; ++page) {
        const auto pageText = document.getAllText(page).text().trimmed();
        if (pageText.isEmpty()) continue;
        if (!text.isEmpty()) text.append('\n');
        text.append(pageText.left(characters - text.size()));
    }
    if (text.isEmpty()) return {{}, ExcerptIssue::PdfNoText};
    return {std::move(text)};
}
#endif

#ifdef Q_OS_WIN
constexpr UINT koreanCodePage = 949;

// 시스템 로캘과 무관하게 Windows 한국어 코드 페이지로 바이트를 검사하는 함수.
QString decodeCp949(QByteArrayView bytes) {
    const int length = static_cast<int>(bytes.size());
    const int size = MultiByteToWideChar(koreanCodePage, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
    if (size == 0) return {};
    std::wstring decoded(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(koreanCodePage, MB_ERR_INVALID_CHARS, bytes.data(), length, decoded.data(), size)) return {};
    return QString::fromStdWString(decoded);
}
#endif

// 연결 경로를 제외하고 로컬 문서의 본문 앞부분을 가져오는 함수.
Excerpt documentExcerpt(const QFileInfo& info, qsizetype characters) {
    const auto extension = info.suffix().toLower();
    if (!info.isFile() || info.isSymLink() || !isDocumentExtension(extension)) return {{}, ExcerptIssue::Unreadable};
    if (extension == "pdf") {
#ifdef NOOKBIT_DOCUMENT_PDF
        return pdfExcerpt(info.absoluteFilePath(), characters);
#else
        return {{}, ExcerptIssue::PdfUnavailable};
#endif
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {{}, ExcerptIssue::Unreadable};
    const auto bytes = file.read(characters * 4);
    if (file.error() != QFileDevice::NoError) return {{}, ExcerptIssue::Unreadable};
    if (bytes.contains('\0')) return {{}, ExcerptIssue::Encoding};

    const bool partial = !file.atEnd();
    QStringDecoder decode(QStringDecoder::Utf8, partial
        ? QStringConverter::Flag::Default : QStringConverter::Flag::Stateless);
    QString text = decode(bytes);
    if (decode.hasError()) {
#ifdef Q_OS_WIN
        if (bytes.startsWith("\xef\xbb\xbf")) return {{}, ExcerptIssue::Encoding};
        text = decodeCp949(bytes);
        // 제한 길이에서 마지막 2바이트 문자가 끊긴 경우 그 선행 바이트만 제외한다.
        if (text.isNull() && partial && !bytes.isEmpty() &&
            IsDBCSLeadByteEx(koreanCodePage, static_cast<BYTE>(bytes.back()))) {
            text = decodeCp949(QByteArrayView(bytes).chopped(1));
        }
        if (text.isNull()) return {{}, ExcerptIssue::Encoding};
#else
        return {{}, ExcerptIssue::Encoding};
#endif
    }
    text = text.left(characters).trimmed();
    if (text.isEmpty()) return {{}, ExcerptIssue::Empty};
    return {std::move(text)};
}

// 대기 문서가 자기 자신의 예시가 되지 않도록 정규 경로를 비교하는 함수.
QString documentKey(const QFileInfo& info) {
    const auto path = info.canonicalFilePath();
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

} // namespace

// 문서 추천에서 검사하는 확장자인지 확인하는 함수.
bool isDocumentExtension(const QString& extension) {
    return extension == "txt" || extension == "md" || extension == "pdf";
}

// 규칙에 적힌 키워드를 분류별 기준으로 삼아 실제 문서의 본문을 비교한다.
std::optional<RuleEngine::DocumentMatch> scoreDocument(const FileFact& file, const DocumentAnalysis& analysis) {
    if (file.absolutePath.empty() || !file.absolutePath.is_absolute()) return std::nullopt;
    const auto excerpt = documentExcerpt(QFileInfo(QString::fromStdWString(file.absolutePath.wstring())), 4000);
    if (excerpt.issue != ExcerptIssue::None) return std::nullopt;

    std::vector<Profile> profiles;
    profiles.reserve(analysis.categories.size());
    for (const auto& category : analysis.categories) {
        profiles.push_back({QString::fromStdString(category.id).toStdU16String(),
                            countTextTerms(QString::fromStdString(category.keywords)), {}});
    }
    const auto idf = prepareProfiles(profiles);
    const auto prediction = recommend(countTextTerms(excerpt.text), profiles, idf);
    if (!prediction.bestScore) return std::nullopt;
    const auto matched = std::ranges::find(profiles, prediction.ranked.front(), &Profile::label);
    return RuleEngine::DocumentMatch{static_cast<std::size_t>(matched - profiles.begin()), *prediction.bestScore};
}

// 사용자가 체크할 수 있는 바로 아래 폴더 이름을 가져오는 함수.
QFileInfoList candidateFolders(const QString& root) {
    if (root.isEmpty()) return {};
    auto folders = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    folders.removeIf([](const QFileInfo& folder) { return folder.isJunction(); });
    return folders;
}

// 후보 폴더의 대표 문서를 읽고 이동 계획과 별개로 추천하는 함수.
FolderSuggestions suggestFolders(const QString& homePath, const QStringList& documents,
                                 const QStringList& previewSources,
                                 const std::optional<QStringList>& selectedFolders) {
    constexpr double minimumSimilarity = 0.286;
    QSet<QString> excluded;
    for (const auto& path : previewSources) excluded.insert(documentKey(QFileInfo(path)));
    std::vector<Profile> profiles;
    auto folders = candidateFolders(homePath);
    if (selectedFolders) folders.removeIf([&](const QFileInfo& folder) {
        return !selectedFolders->contains(folder.fileName());
    });
    FolderSuggestions result;
    result.candidateCount = folders.size();
    result.inspectedFolderCount = std::min(result.candidateCount, recommendationFolderLimit);
    folders.resize(result.inspectedFolderCount);
    QStringList examplePatterns{QStringLiteral("*.txt"), QStringLiteral("*.md")};
#ifdef NOOKBIT_DOCUMENT_PDF
    examplePatterns.append(QStringLiteral("*.pdf"));
#endif
    for (const auto& folder : folders) {
        const QDir directory(folder.absoluteFilePath());
        QStringList examples;
        int attempts = 0;
        const auto files = directory.entryInfoList(examplePatterns,
            QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const auto& file : files) {
            if (excluded.contains(documentKey(file))) continue;
            if (attempts == exampleReadLimit) {
                ++result.cappedFolderCount;
                break;
            }
            ++attempts;
            const auto excerpt = documentExcerpt(file, 1200);
            if (excerpt.issue == ExcerptIssue::None && !examples.contains(excerpt.text)) examples.append(excerpt.text);
            if (examples.size() == 2) break;
        }
        if (examples.isEmpty()) continue;
        profiles.push_back({folder.absoluteFilePath().toStdU16String(),
                            countTextTerms(folder.fileName() + '\n' + examples.join('\n')), {}});
    }

    result.usedFolderCount = static_cast<qsizetype>(profiles.size());
    const auto idf = prepareProfiles(profiles);
    for (const auto& source : documents) {
        const QFileInfo file(source);
        const auto excerpt = documentExcerpt(file, 4000);
        if (excerpt.issue != ExcerptIssue::None) {
            result.unreadableDocuments.insert(source, excerpt.issue);
            continue;
        }
        if (profiles.empty()) continue;
        const auto prediction = recommend(countTextTerms(file.fileName() + '\n' + excerpt.text), profiles, idf);
        if (prediction.bestScore && *prediction.bestScore >= minimumSimilarity)
            result.destinations.insert(source, QString::fromStdU16String(prediction.ranked.front()));
    }
    return result;
}

} // namespace nookbit::document_recommendation
