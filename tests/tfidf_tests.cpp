#include "test_expect.hpp"

#include "modules/document_recommendation/tfidf.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

using nookbit::document_recommendation::Profile;
using nookbit::document_recommendation::prepareProfiles;
using nookbit::document_recommendation::recommend;

using nookbit::test::expect;
using nookbit::test::failures;

void testSimilarity() {
    std::vector<Profile> profiles{
        {u"minutes", {{u"공통", 1}, {u"회의", 2}}, {}},
        {u"manual", {{u"공통", 1}, {u"설정", 1}}, {}},
    };
    const auto idf = prepareProfiles(profiles);
    const double meetingWeight = (1.0 + std::log(2.0)) * (1.0 + std::log(1.5));
    const double expected = meetingWeight / std::hypot(1.0, meetingWeight);
    const auto result = recommend({{u"회의", 1}, {u"unseen", 10}}, profiles, idf);
    expect(result.ranked == std::vector<std::u16string>{u"minutes"}, "return only matching profiles");
    expect(result.bestScore && std::abs(*result.bestScore - expected) < 1e-12,
           "apply log term frequency, smoothed IDF and cosine normalization");

    const auto unmatched = recommend({{u"unseen", 1}}, profiles, idf);
    expect(unmatched.ranked.empty() && !unmatched.bestScore, "leave a query with no shared terms unranked");
}

void testEqualScoresKeepInputOrder() {
    std::vector<Profile> profiles{
        {u"C", {{u"ab", 1}}, {}},
        {u"B", {{u"ab", 1}}, {}},
        {u"D", {{u"ab", 1}}, {}},
        {u"A", {{u"ab", 1}}, {}},
    };
    const auto idf = prepareProfiles(profiles);
    const auto result = recommend({{u"ab", 1}}, profiles, idf);
    expect(result.ranked == std::vector<std::u16string>{u"C", u"B", u"D"},
           "preserve input order for equal scores and return at most three profiles");
    expect(result.bestScore == 1.0, "identical vectors have full similarity");
}

} // namespace

int main() {
    testSimilarity();
    testEqualScoresKeepInputOrder();
    return failures == 0 ? 0 : 1;
}
