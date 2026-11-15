#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nookbit::document_recommendation {

using Terms = std::map<std::u16string, double>;
struct Prediction {
    std::vector<std::u16string> ranked;
    std::optional<double> bestScore;
};

struct Profile {
    std::u16string label;
    Terms counts;
    Terms weights;
};

// 입력 빈도에는 실제로 등장한 조각과 그 개수만 포함한다.
Terms prepareProfiles(std::vector<Profile>& profiles);
Prediction recommend(const Terms& counts, const std::vector<Profile>& profiles, const Terms& idf);

} // namespace nookbit::document_recommendation
