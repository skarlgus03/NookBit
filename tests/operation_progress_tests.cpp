#include <nookbit/application/operation_progress.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>

namespace {
int failures{};
// 기대한 진행 데이터가 아니면 실패를 표시하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}

// 화면 없는 코어에서 진행 값·제한·수명·예외 격리를 검증하는 함수.
int main() {
    using namespace nookbit;
    std::vector<OperationProgress> events;
    {
        OperationProgressScope scope([&](const OperationProgress& value) { events.push_back(value); }, std::chrono::hours(1));
        OperationProgressScope::begin(OperationPhase::CheckingSource, 2);
        OperationProgressScope::file(1, 2, std::filesystem::path("private-parent") / "example.bin");
        expect(events.back().fileName == "example.bin" && events.back().fileIndex == 1, "basename and file order only");
        OperationProgressScope::copied(0, 100);
        const auto initialCount = events.size();
        for (unsigned i = 1; i < 100; ++i) OperationProgressScope::copied(i, 100);
        expect(events.size() == initialCount, "byte updates throttled without sleeping");
        OperationProgressScope::copied(100, 100);
        expect(events.size() == initialCount + 1 && events.back().copiedBytes == 100 && events.back().byteProgressKnown,
            "final copy bytes always delivered");
        expect(events.back().completedFiles == 0, "copy 100 percent is not file completion");
        OperationProgressScope::phase(OperationPhase::VerifyingCopy);
        expect(!events.back().byteProgressKnown && events.back().completedFiles == 0, "verification has no invented percent");
        OperationProgressScope::phase(OperationPhase::SavingHistory);
        OperationProgressScope::fileCompleted();
        expect(events.back().completedFiles == 1 && events.back().phase != OperationPhase::Completed, "one recorded file not whole request completion");
        OperationProgressScope::file(2, 2, "second.bin");
        const std::uintmax_t large = std::uintmax_t{5} * 1024 * 1024 * 1024;
        OperationProgressScope::copied(large, large);
        expect(events.back().totalBytes == large && events.back().copiedBytes == large, "progress sizes exceed 4 GiB safely");
        OperationProgressScope::copied(large + 1, large);
        expect(events.back().copiedBytes == large, "ratio never exceeds total");
        OperationProgressScope::phase(OperationPhase::FlushingCopy);
        expect(!events.back().byteProgressKnown, "flush clears completed copy bar");
        OperationProgressScope::copied(0, 0);
        expect(!events.back().byteProgressKnown, "zero length never divides by zero");
        const auto previousCount = events.size();
        std::thread unrelated([] { OperationProgressScope::phase(OperationPhase::Failed); }); unrelated.join();
        expect(events.size() == previousCount, "unrelated thread cannot publish to request");
        std::vector<OperationProgress> nested;
        {
            OperationProgressScope inner([&](const OperationProgress& value) { nested.push_back(value); });
            OperationProgressScope::begin(OperationPhase::Preparing);
        }
        OperationProgressScope::phase(OperationPhase::RemovingSource);
        expect(nested.size() == 1 && events.back().fileName == "second.bin" && events.back().completedFiles == 1,
            "nested request restores outer context without erasing state");
        OperationProgressScope::fileCompleted(); OperationProgressScope::fileCompleted();
        expect(events.back().completedFiles == 2, "completed count bounded by request size");
    }
    const auto finishedCount = events.size();
    OperationProgressScope::begin(OperationPhase::Starting);
    OperationProgressScope::copied(1, 1);
    expect(events.size() == finishedCount, "scope destruction unregisters observer");
    {
        OperationProgressScope throwing([](const OperationProgress&) { throw std::runtime_error("owned observer failure"); });
        OperationProgressScope::begin(OperationPhase::Copying);
        OperationProgressScope::file(1, 1, "owned.bin");
        OperationProgressScope::copied(1, 1);
        OperationProgressScope::fileCompleted();
    }
    return failures ? 1 : 0;
}
