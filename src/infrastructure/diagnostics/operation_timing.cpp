#include "operation_timing.hpp"
#include <fstream>
#include <iomanip>
#include <locale>
#include <utility>

namespace nookbit {
thread_local OperationTiming* OperationTiming::current_ = nullptr;
// 요청별 수명 안에서만 하위 파일 일꾼의 측정을 수집하는 함수.
OperationTiming::OperationTiming(std::filesystem::path root, const char* operation, std::size_t files, std::uintmax_t bytes)
    : previous_(current_), root_(std::move(root)), operation_(operation),
      unixStartedMs_(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count()), files_(files), bytes_(bytes) {
    current_ = this;
}
// 최신 요청만 작은 로그로 저장하며 저장 지연은 total_ms에 포함하지 않는 함수.
OperationTiming::~OperationTiming() noexcept {
    const auto total = std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
    current_ = previous_;
    try {
        std::filesystem::create_directories(root_);
        const auto name = std::string("last-") + operation_ + "-timing.log";
        std::ofstream report(root_ / name, std::ios::binary | std::ios::trunc);
        if (!report) return;
        report.imbue(std::locale::classic());
        report << std::fixed << std::setprecision(3)
            << "NOOKBIT_TIMING_V1\noperation=" << operation_ << " outcome=" << outcome_
            << " unix_started_ms=" << unixStartedMs_ << " files=" << files_ << " planned_bytes=" << bytes_
            << " total_ms=" << total << '\n'
            << "# Nested stages overlap; do not sum parents and children. No file contents or paths recorded.\n"
            << "stage\tdepth\tduration_ms\tplanned_bytes\n";
        for (const auto& entry : entries_)
            report << entry.stage << '\t' << entry.depth << '\t' << entry.ms << '\t' << entry.bytes << '\n';
    } catch (...) { /* 진단 실패로 이동 결과를 변경하지 않는다. */ }
}
// 진단 문맥이 없는 호출에는 시계 조회·파일 기록을 추가하지 않는 함수.
TimingStage::TimingStage(const char* name, std::uintmax_t bytes) noexcept
    : owner_(OperationTiming::current_), name_(name), bytes_(bytes) {
    if (owner_) { depth_ = owner_->depth_++; started_ = std::chrono::steady_clock::now(); }
}
// 메모리에 구간 하나만 추가하며 기록 메모리 부족도 실제 작업과 분리하는 함수.
void TimingStage::stop() noexcept {
    if (!owner_) return;
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count();
    --owner_->depth_;
    try { owner_->entries_.push_back({name_, depth_, elapsed, bytes_}); } catch (...) {}
    owner_ = nullptr;
}
}
