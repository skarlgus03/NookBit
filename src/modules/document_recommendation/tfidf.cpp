#include "modules/document_recommendation/tfidf.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nookbit::document_recommendation {
namespace {

// 후보 폴더 설명에서 학습한 IDF로 텍스트를 단위 벡터로 바꾸는 함수.
Terms vectorize(const Terms& counts, const Terms& idf) {
    Terms weighted;
    double squaredNorm = 0;
    for (const auto& [term, count] : counts) {
        const auto frequency = idf.find(term);
        if (frequency == idf.end()) continue;
        const double weight = (1.0 + std::log(count)) * frequency->second;
        weighted.emplace(term, weight);
        squaredNorm += weight * weight;
    }
    const double norm = std::sqrt(squaredNorm);
    for (auto& entry : weighted) entry.second /= norm;
    return weighted;
}

} // namespace

// 정답 문서를 읽지 않고 후보 폴더 설명만으로 TF-IDF 기준을 준비하는 함수.
Terms prepareProfiles(std::vector<Profile>& profiles) {
    Terms idf;
    for (const auto& profile : profiles) {
        for (const auto& entry : profile.counts) ++idf[entry.first];
    }
    for (auto& entry : idf)
        entry.second = 1.0 + std::log((1.0 + static_cast<double>(profiles.size())) / (1.0 + entry.second));
    for (auto& profile : profiles) profile.weights = vectorize(profile.counts, idf);
    return idf;
}

// 유사도가 있는 폴더를 순위대로 최대 3개 반환하는 함수.
Prediction recommend(const Terms& counts, const std::vector<Profile>& profiles, const Terms& idf) {
    const auto query = vectorize(counts, idf);
    std::vector<std::pair<double, std::u16string>> scored;
    for (const auto& profile : profiles) {
        double score = 0;
        for (const auto& [term, weight] : query) {
            const auto match = profile.weights.find(term);
            if (match != profile.weights.end()) score += weight * match->second;
        }
        if (score > 0) scored.emplace_back(score, profile.label);
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const auto& left, const auto& right) { return left.first > right.first; });
    Prediction result;
    if (!scored.empty()) result.bestScore = scored.front().first;
    for (std::size_t i = 0; i < std::min<std::size_t>(3, scored.size()); ++i)
        result.ranked.push_back(scored[i].second);
    return result;
}

} // namespace nookbit::document_recommendation
