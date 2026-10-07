#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace nookbit {
enum class OperationPhase {
    Starting, Preparing, Approving, CheckingPlan, CheckingSource, ProtectingSource,
    Copying, FlushingCopy, VerifyingCopy, PreservingMetadata, CommittingDestination,
    VerifyingBeforeRemoval, RemovingSource, Moving, CheckingResult, SavingHistory,
    Restoring, Recovering, Completed, Failed
};

/// 파일 내용이나 화면 타입이 없는 값 스냅샷이며 바이트 비율은 현재 파일의 복사에만 해당한다.
struct OperationProgress {
    OperationPhase phase{OperationPhase::Starting};
    std::size_t fileIndex{}, fileCount{}, completedFiles{};
    std::string fileName;
    std::uintmax_t copiedBytes{}, totalBytes{};
    bool byteProgressKnown{false};
};

/// 요청 조립부가 진행 수신자를 연결하며 다른 스레드·다른 요청과 문맥을 공유하지 않는다.
class OperationProgressScope final {
public:
    using Sink = std::function<void(const OperationProgress&)>;
    /// 요청 동안만 진행 이벤트를 전달하고 바이트 알림을 기본 100ms 간격으로 제한하는 함수.
    explicit OperationProgressScope(Sink sink, std::chrono::milliseconds byteInterval = std::chrono::milliseconds(100))
        : previous_(active_), sink_(std::move(sink)), interval_(byteInterval) { active_ = this; }
    /// 이전 문맥만 복구하며 성공·실패나 파일 완료를 임의로 만들어내지 않는 함수.
    ~OperationProgressScope() { active_ = previous_; }
    OperationProgressScope(const OperationProgressScope&) = delete;
    OperationProgressScope& operator=(const OperationProgressScope&) = delete;
    /// 새 처리 구간의 파일 수와 완료 수를 초기화하는 함수.
    static void begin(OperationPhase phase, std::size_t files = 0) noexcept {
        if (!active_) return;
        active_->value_ = {}; active_->value_.phase = phase; active_->value_.fileCount = files;
        active_->send();
    }
    /// 단계 변경은 즉시 전달하고 이전 복사 비율은 지우는 함수.
    static void phase(OperationPhase phase) noexcept {
        if (!active_) return;
        auto& value = active_->value_;
        if (value.phase == phase && !value.byteProgressKnown) return;
        value.phase = phase; value.byteProgressKnown = false; value.copiedBytes = value.totalBytes = 0;
        active_->send();
    }
    /// 실제 파일 작업의 순서·개수·파일명만 전달하는 함수.
    static void file(std::size_t index, std::size_t count, const std::filesystem::path& path) noexcept {
        if (!active_) return;
        try {
            auto& value = active_->value_;
            const auto name = path.filename().u8string();
            value.fileName.assign(name.begin(), name.end());
            value.fileCount = count; value.fileIndex = std::min(index, count);
            value.byteProgressKnown = false; value.copiedBytes = value.totalBytes = 0;
            active_->send();
        } catch (...) { /* 진행 표시 실패로 파일 작업을 변경하지 않는다. */ }
    }
    /// 기록까지 확인된 파일 하나의 성공 완료만 누적하는 함수.
    static void fileCompleted() noexcept {
        if (!active_) return;
        auto& value = active_->value_;
        if (value.completedFiles < value.fileCount) ++value.completedFiles;
        active_->send();
    }
    /// 쓰기에 성공한 바이트만 알리며 최초·마지막 복사 수치는 제한 없이 전달하는 함수.
    static void copied(std::uintmax_t bytes, std::uintmax_t total) noexcept {
        if (!active_) return;
        auto& scope = *active_;
        auto& value = scope.value_;
        const bool first = value.phase != OperationPhase::Copying || !value.byteProgressKnown;
        value.phase = OperationPhase::Copying; value.totalBytes = total;
        value.copiedBytes = std::min(bytes, total); value.byteProgressKnown = total != 0;
        const auto now = std::chrono::steady_clock::now();
        if (first || bytes >= total || now - scope.lastByteUpdate_ >= scope.interval_) {
            scope.lastByteUpdate_ = now; scope.send();
        }
    }
private:
    /// 관찰자 예외를 삼켜 진행 수신 실패가 이동 실패가 되지 않게 하는 함수.
    void send() noexcept { try { if (sink_) sink_(value_); } catch (...) {} }
    inline static thread_local OperationProgressScope* active_{nullptr};
    OperationProgressScope* previous_;
    Sink sink_;
    std::chrono::milliseconds interval_;
    std::chrono::steady_clock::time_point lastByteUpdate_{};
    OperationProgress value_;
};
}
