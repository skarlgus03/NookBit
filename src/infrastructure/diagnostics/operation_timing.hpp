#pragma once
#include <chrono>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace nookbit {
class TimingStage;
/// 요청 스레드에만 속한 진단 기록이며 파일 내용·경로·승인 계약을 보관하지 않는다.
class OperationTiming final {
    friend class TimingStage;
    using Clock = std::chrono::steady_clock;
    struct Entry { const char* stage; unsigned depth; double ms; std::uintmax_t bytes; };
    static thread_local OperationTiming* current_;
    OperationTiming* previous_;
    std::filesystem::path root_;
    const char* operation_;
    const char* outcome_{"incomplete"};
    Clock::time_point started_{Clock::now()};
    std::int64_t unixStartedMs_;
    std::size_t files_;
    std::uintmax_t bytes_;
    unsigned depth_{};
    std::vector<Entry> entries_;
public:
    /// 이동이나 준비 요청의 단조 시계와 현재 스레드 진단 문맥을 만드는 함수.
    OperationTiming(std::filesystem::path root, const char* operation, std::size_t files, std::uintmax_t bytes);
    /// 최종 결과를 저장하고 이전 문맥을 복구하며 진단 오류는 작업에 전달하지 않는 함수.
    ~OperationTiming() noexcept;
    OperationTiming(const OperationTiming&) = delete;
    OperationTiming& operator=(const OperationTiming&) = delete;
    /// 실제 작업의 성공·실패만 기록하며 로그 저장 성공 여부와 분리하는 함수.
    void completed(bool success) noexcept { outcome_ = success ? "success" : "failure"; }
};

/// 활성 요청이 있을 때만 한 구간의 시간을 모으며 파일 반복문에서는 생성하지 않는다.
class TimingStage final {
    OperationTiming* owner_;
    const char* name_;
    std::uintmax_t bytes_;
    unsigned depth_{};
    std::chrono::steady_clock::time_point started_{};
public:
    /// 현재 요청의 측정 구간을 시작하거나 문맥이 없으면 기록을 생략하는 함수.
    explicit TimingStage(const char* name, std::uintmax_t bytes = 0) noexcept;
    /// 조기 반환이나 예외도 구간 종료로 기록하는 함수.
    ~TimingStage() noexcept { stop(); }
    TimingStage(const TimingStage&) = delete;
    TimingStage& operator=(const TimingStage&) = delete;
    /// 구간을 한 번만 종료하며 남은 함수 실행은 측정에서 제외하는 함수.
    void stop() noexcept;
};
}
