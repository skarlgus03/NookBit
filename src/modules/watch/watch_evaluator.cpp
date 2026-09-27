#include "modules/watch/watch_evaluator.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace nookbit {

// 현재 파일 집합과 시각이 사용자 조건을 만족하는지 계산하는 함수.
Result<WatchEvaluation> WatchEvaluator::evaluate(
    const WatchSettings& settings,
    const std::vector<FileFact>& files,
    std::int64_t nowUtcMs) {
    if (settings.conditions.empty()) {
        return Result<WatchEvaluation>::failure({
            "watch.conditions_empty", "At least one watch condition is required.",
        });
    }
    std::set<WatchConditionType> seen;
    for (const auto& condition : settings.conditions) {
        if (condition.threshold < 1 || !seen.insert(condition.type).second) {
            return Result<WatchEvaluation>::failure({
                "watch.condition_invalid", "Watch conditions must be positive and unique.",
            });
        }
    }
    WatchEvaluation evaluation;
    evaluation.fileCount = files.size();
    for (const auto& file : files) {
        if (file.fingerprint.size >
            std::numeric_limits<std::uint64_t>::max() - evaluation.totalBytes) {
            return Result<WatchEvaluation>::failure({
                "watch.size_overflow", "The scanned file sizes exceed the supported range.",
            });
        }
        evaluation.totalBytes += static_cast<std::uint64_t>(file.fingerprint.size);
        if (file.fingerprint.modifiedUtcMs < nowUtcMs) {
            const auto elapsedMinutes =
                (static_cast<long double>(nowUtcMs) -
                 static_cast<long double>(file.fingerprint.modifiedUtcMs)) / 60000.0L;
            evaluation.oldestAgeMinutes = std::max(
                evaluation.oldestAgeMinutes,
                static_cast<std::int64_t>(std::min(
                    elapsedMinutes,
                    static_cast<long double>(std::numeric_limits<std::int64_t>::max()))));
        }
    }
    // 빈 폴더에서는 시간·크기 조건이 참이 되어도 제안을 만들지 않는다.
    if (files.empty()) return Result<WatchEvaluation>::success(evaluation);
    bool matched = settings.combination == WatchCombination::All;
    for (const auto& condition : settings.conditions) {
        bool conditionMet = false;
        switch (condition.type) {
        case WatchConditionType::FileCount:
            conditionMet = evaluation.fileCount >= static_cast<std::size_t>(condition.threshold);
            break;
        case WatchConditionType::TotalSizeMiB:
            conditionMet = evaluation.totalBytes >=
                static_cast<std::uint64_t>(condition.threshold) * 1024ULL * 1024ULL;
            break;
        case WatchConditionType::OldestAgeMinutes:
            conditionMet = evaluation.oldestAgeMinutes >= condition.threshold;
            break;
        }
        if (settings.combination == WatchCombination::All) matched = matched && conditionMet;
        else matched = matched || conditionMet;
    }
    evaluation.shouldOffer = matched;
    return Result<WatchEvaluation>::success(evaluation);
}

} // namespace nookbit
