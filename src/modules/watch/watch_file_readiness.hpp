#pragma once

#include <nookbit/domain/watch.hpp>

#include <map>

namespace nookbit {

struct WatchReadiness {
    std::vector<FileFact> readyFiles;
    std::size_t waitingCount{0};
    int nextCheckDelayMs{0};
};

/// 파일 내용을 읽지 않고 연속 관찰한 메타데이터로 감시 후보의 준비 여부를 판단한다.
class WatchFileReadiness final {
public:
    /// 기본 30초의 안정 대기 시간을 보관하는 생성자.
    explicit WatchFileReadiness(int stableWindowMs = 30000);
    /// 현재 파일을 관찰하고 일정 시간 변하지 않은 파일만 반환하는 함수.
    [[nodiscard]] WatchReadiness observe(const std::vector<FileFact>& files, std::int64_t nowUtcMs);
    /// 폴더 변경이나 검사 실패 시 이전 관찰 결과를 지우는 함수.
    void reset();

private:
    struct Observation {
        FileFingerprint fingerprint;
        std::int64_t stableSinceUtcMs{0};
    };
    int stableWindowMs_;
    std::int64_t lastObservedUtcMs_{0};
    std::map<std::wstring, Observation> observations_;
};

} // namespace nookbit
