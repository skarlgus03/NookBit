#pragma once

#include "modules/rules/rule_engine.hpp"

#include <QFileInfo>
#include <QMap>
#include <QStringList>

#include <optional>

namespace nookbit::document_recommendation {

enum class ExcerptIssue { None, Unreadable, Encoding, Empty, PdfNoText, PdfUnavailable };

struct FolderSuggestions {
    QMap<QString, QString> destinations;
    QMap<QString, ExcerptIssue> unreadableDocuments;
    qsizetype candidateCount{0};
    qsizetype inspectedFolderCount{0};
    qsizetype usedFolderCount{0};
    qsizetype cappedFolderCount{0};
};

inline constexpr qsizetype recommendationFolderLimit = 32;
inline constexpr int exampleReadLimit = 8;

bool isDocumentExtension(const QString& extension);
std::optional<RuleEngine::DocumentMatch> scoreDocument(const FileFact& file, const DocumentAnalysis& analysis);
QFileInfoList candidateFolders(const QString& root);
FolderSuggestions suggestFolders(const QString& homePath, const QStringList& documents,
                                 const QStringList& previewSources,
                                 const std::optional<QStringList>& selectedFolders = std::nullopt);

} // namespace nookbit::document_recommendation
