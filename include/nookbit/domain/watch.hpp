#pragma once

#include <nookbit/domain/model.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nookbit {

enum class WatchConditionType { FileCount, TotalSizeMiB, OldestAgeMinutes, TotalSize = TotalSizeMiB };
enum class WatchCombination { All, Any };

struct WatchCondition {
    WatchConditionType type{WatchConditionType::FileCount};
    int threshold{1};
    std::string sizeUnit{"MiB"}; // 기존 설정은 단위 필드 없이 MiB로 해석한다.
};

// 지원하는 전체 크기 단위의 바이트 배수를 반환하고 잘못된 단위는 거부하는 함수.
inline std::uint64_t watchSizeUnitBytes(const std::string& unit) {
    if (unit == "KB") return 1000ULL;
    if (unit == "MB") return 1000000ULL;
    if (unit == "GB") return 1000000000ULL;
    if (unit == "MiB") return 1048576ULL;
    return 0;
}

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
    std::size_t waitingFileCount{0};
    int nextCheckDelayMs{0};
};

} // namespace nookbit
