#pragma once

#include "modules/document_recommendation/tfidf.hpp"

#include <QRegularExpression>
#include <QString>

namespace nookbit::document_recommendation {

// 한글과 영문 모두에서 단어 내부의 2~3글자 조각 빈도를 세는 함수.
inline Terms countTextTerms(const QString& text) {
    static const QRegularExpression words(QStringLiteral("[\\p{L}\\p{N}]+"));
    const auto normalized = text.normalized(QString::NormalizationForm_C).toCaseFolded();
    auto matches = words.globalMatch(normalized);
    Terms counts;
    while (matches.hasNext()) {
        const auto word = matches.next().captured().toStdU16String();
        if (word.size() == 1) ++counts[word];
        for (std::size_t length : {2, 3}) {
            for (std::size_t offset = 0; offset + length <= word.size(); ++offset)
                ++counts[word.substr(offset, length)];
        }
    }
    return counts;
}

} // namespace nookbit::document_recommendation
