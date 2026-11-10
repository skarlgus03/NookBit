#pragma once

#include <QMap>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace nookbit::document_recommendation {

using Terms = QMap<QString, double>;
struct Prediction {
    QStringList ranked;
    std::optional<double> bestScore;
};

// 한글과 영문 모두에서 단어 내부의 2~3글자 조각 빈도를 세는 함수.
inline Terms termCounts(const QString& text) {
    static const QRegularExpression words(QStringLiteral("[\\p{L}\\p{N}]+"));
    const auto normalized = text.normalized(QString::NormalizationForm_C).toCaseFolded();
    auto matches = words.globalMatch(normalized);
    Terms counts;
    while (matches.hasNext()) {
        const auto word = matches.next().captured();
        if (word.size() == 1) ++counts[word];
        for (qsizetype length : {2, 3}) {
            for (qsizetype offset = 0; offset + length <= word.size(); ++offset)
                ++counts[word.mid(offset, length)];
        }
    }
    return counts;
}

// 후보 폴더 설명에서 학습한 IDF로 텍스트를 단위 벡터로 바꾸는 함수.
inline Terms vectorize(const QString& text, const Terms& idf) {
    Terms weighted;
    double squaredNorm = 0;
    const auto counts = termCounts(text);
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const double weight = (1.0 + std::log(it.value())) * idf.value(it.key());
        if (weight == 0) continue;
        weighted.insert(it.key(), weight);
        squaredNorm += weight * weight;
    }
    const double norm = std::sqrt(squaredNorm);
    for (auto& weight : weighted) weight /= norm;
    return weighted;
}

struct Profile {
    QString folder;
    QString description;
    Terms vector;
};

// 정답 문서를 읽지 않고 후보 폴더 설명만으로 TF-IDF 기준을 준비하는 함수.
inline Terms prepareProfiles(std::vector<Profile>& profiles) {
    Terms idf;
    for (const auto& profile : profiles) {
        const auto counts = termCounts(profile.description);
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) ++idf[it.key()];
    }
    for (auto& frequency : idf)
        frequency = 1.0 + std::log((1.0 + static_cast<double>(profiles.size())) / (1.0 + frequency));
    for (auto& profile : profiles) profile.vector = vectorize(profile.description, idf);
    return idf;
}

// 유사도가 있는 폴더를 순위대로 최대 3개 반환하는 함수.
inline Prediction recommend(const QString& text, const std::vector<Profile>& profiles, const Terms& idf) {
    const auto query = vectorize(text, idf);
    std::vector<std::pair<double, QString>> scored;
    for (const auto& profile : profiles) {
        double score = 0;
        for (auto it = query.cbegin(); it != query.cend(); ++it)
            score += it.value() * profile.vector.value(it.key());
        if (score > 0) scored.emplace_back(score, profile.folder);
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const auto& left, const auto& right) { return left.first > right.first; });
    Prediction result;
    if (!scored.empty()) result.bestScore = scored.front().first;
    for (std::size_t i = 0; i < std::min<std::size_t>(3, scored.size()); ++i)
        result.ranked.append(scored[i].second);
    return result;
}

} // namespace nookbit::document_recommendation
