#pragma once
#include <nookbit/domain/result.hpp>
#include <filesystem>
#include <vector>

namespace nookbit {
struct PendingPathRejection {
    std::filesystem::path path;
    Error error;
};

/// 경로별 등록 결과이며 실제 파일 이동이나 승인을 포함하지 않는다.
struct PendingEnqueueResult {
    std::vector<std::filesystem::path> added;
    std::vector<PendingPathRejection> rejected;
};

class IPendingPathsQuery {
public:
    /// 조회 구현체를 인터페이스로 해제하는 함수.
    virtual ~IPendingPathsQuery() = default;
    /// 대기 경로의 값 복사본을 반환하고 목록은 변경하지 않는 함수.
    [[nodiscard]] virtual Result<std::vector<std::filesystem::path>> pendingPaths() const = 0;
};

class IEnqueuePendingPaths {
public:
    /// 등록 구현체를 인터페이스로 해제하는 함수.
    virtual ~IEnqueuePendingPaths() = default;
    /// 파일을 옮기지 않고 등록하며 일부 거부도 경로별 결과로 반환하는 함수.
    /// 요청 전체의 실패는 등록 전에 반환하며 등록을 시도했다면 경로별 결과를 제공한다.
    [[nodiscard]] virtual Result<PendingEnqueueResult> enqueuePaths(
        const std::vector<std::filesystem::path>& paths) = 0;
};
}
