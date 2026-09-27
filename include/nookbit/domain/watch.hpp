#pragma once

#include <nookbit/domain/model.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nookbit {

enum class WatchConditionType { FileCount, TotalSizeMiB, OldestAgeMinutes };
enum class WatchCombination { All, Any };

struct WatchCondition {
    WatchConditionType type{WatchConditionType::FileCount};
    int threshold{1};
};

struct WatchSettings {
    std::filesystem::path folder;
    bool enabled{false};
    bool recursive{false};
    int rescanIntervalMinutes{30};
    WatchCombination combination{WatchCombination::Any};
    std::vector<WatchCondition> conditions;
    std::vector<std::string> excludePatterns;
};

struct WatchEvaluation {
    bool shouldOffer{false};
    std::size_t fileCount{0};
    std::uint64_t totalBytes{0};
    std::int64_t oldestAgeMinutes{0};
};

/// 감시 조건 판단과 화면 제안에 필요한 파일 정보를 묶은 결과.
struct WatchProposal {
    WatchEvaluation evaluation;
    std::vector<FileFact> eligibleFiles;
    std::vector<Error> warnings;
};

} // namespace nookbit
