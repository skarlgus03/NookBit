#pragma once

#include <nookbit/domain/result.hpp>
#include <nookbit/domain/watch.hpp>

#include <cstdint>
#include <vector>

namespace nookbit {

/// 분류 가능한 파일 정보만으로 감시 제안 조건을 판단한다.
class WatchEvaluator final {
public:
    /// 현재 파일 집합과 시각이 사용자 조건을 만족하는지 계산하는 함수.
    [[nodiscard]] static Result<WatchEvaluation> evaluate(
        const WatchSettings& settings,
        const std::vector<FileFact>& files,
        std::int64_t nowUtcMs);
};

} // namespace nookbit
