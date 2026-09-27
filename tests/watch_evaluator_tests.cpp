#include "modules/watch/watch_evaluator.hpp"

#include <iostream>
#include <vector>

namespace {

int failures = 0;

// 예상한 판단 결과와 다르면 테스트 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 테스트용 파일 정보를 지정한 크기와 경과 시간으로 만드는 함수.
nookbit::FileFact fact(std::uintmax_t size, std::int64_t modifiedMs) {
    nookbit::FileFact file;
    file.fingerprint.size = size;
    file.fingerprint.modifiedUtcMs = modifiedMs;
    return file;
}

// 사용자가 고른 조건 조합과 경계값을 검사하는 함수.
void testConditions() {
    constexpr std::int64_t nowMs = 600000;
    nookbit::WatchSettings settings;
    settings.conditions = {
        {nookbit::WatchConditionType::FileCount, 2},
        {nookbit::WatchConditionType::TotalSizeMiB, 2},
        {nookbit::WatchConditionType::OldestAgeMinutes, 5},
    };
    const std::vector<nookbit::FileFact> files = {
        fact(1024 * 1024, nowMs - 4 * 60000),
        fact(1024 * 1024, nowMs - 5 * 60000),
    };
    settings.combination = nookbit::WatchCombination::All;
    const auto all = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(all.hasValue() && all.value().shouldOffer && all.value().fileCount == 2 &&
               all.value().totalBytes == 2 * 1024 * 1024 && all.value().oldestAgeMinutes == 5,
           "all conditions should match exactly at their thresholds");
    settings.conditions[1].threshold = 3;
    const auto notAll = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(notAll.hasValue() && !notAll.value().shouldOffer,
           "all combination should reject one unmet condition");
    settings.combination = nookbit::WatchCombination::Any;
    const auto any = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(any.hasValue() && any.value().shouldOffer,
           "any combination should accept one met condition");
    const auto empty = nookbit::WatchEvaluator::evaluate(settings, {}, nowMs);
    expect(empty.hasValue() && !empty.value().shouldOffer,
           "empty folder must never trigger a proposal");
    settings.conditions.push_back(settings.conditions.front());
    const auto duplicate = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(!duplicate.hasValue(), "duplicate conditions should be rejected");
}

} // namespace

// 감시 조건 판단 테스트를 실행하는 함수.
int main() {
    testConditions();
    if (failures == 0) {
        std::cout << "All watch evaluator tests passed.\n";
        return 0;
    }
    std::cerr << failures << " watch evaluator assertion(s) failed.\n";
    return 1;
}
