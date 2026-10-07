#include "infrastructure/diagnostics/operation_timing.hpp"
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
// 진단 결과가 예상과 다르면 실패로 표시하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 테스트가 만든 작은 로그만 읽는 함수.
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}
// 현재 요청을 구간 중첩과 중복 종료까지 포함해 기록하는 함수.
void success(const std::filesystem::path& root) {
    nookbit::OperationTiming operation(root, "move", 2, 123);
    nookbit::TimingStage parent("execution_total");
    { nookbit::TimingStage child("copy_stream", 123); child.stop(); child.stop(); }
    parent.stop(); operation.completed(true);
}
}

// 시간 로그의 실패 격리·스레드 격리·최신 덮어쓰기·중첩을 검증하는 함수.
int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::current_path() / ("operation-timing-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    success(root);
    auto report = read(root / "last-move-timing.log");
    expect(report.find("operation=move outcome=success") != std::string::npos, "successful outcome");
    expect(report.find("files=2 planned_bytes=123 total_ms=") != std::string::npos, "size and total recorded");
    expect(report.find("copy_stream\t1\t") != std::string::npos && report.find("execution_total\t0\t") != std::string::npos,
        "parent and child depth recorded");
    const auto child = report.find("copy_stream");
    expect(report.find("copy_stream", child + 1) == std::string::npos, "explicit stop records once");
    {
        nookbit::OperationTiming operation(root, "move", 0, 0);
        nookbit::TimingStage stage("failed_check");
        operation.completed(false);
    }
    report = read(root / "last-move-timing.log");
    expect(report.find("outcome=failure") != std::string::npos && report.find("copy_stream") == std::string::npos,
        "failed request replaces latest log instead of growing it");
    try {
        nookbit::OperationTiming operation(root, "prepare", 1, 2);
        nookbit::TimingStage stage("exception_check");
        throw 1;
    } catch (...) {}
    expect(read(root / "last-prepare-timing.log").find("outcome=incomplete") != std::string::npos,
        "exception unwinding records incomplete scope");
    {
        nookbit::OperationTiming outer(root, "move", 0, 0);
        { nookbit::OperationTiming inner(root, "prepare", 0, 0); inner.completed(true); }
        nookbit::TimingStage stage("restored_outer"); outer.completed(true);
    }
    expect(read(root / "last-move-timing.log").find("restored_outer\t0\t") != std::string::npos, "nested context restored");
    {
        nookbit::OperationTiming outer(root, "move", 0, 0);
        std::thread other([] { nookbit::TimingStage stage("unrelated_thread"); }); other.join();
        outer.completed(true);
    }
    expect(read(root / "last-move-timing.log").find("unrelated_thread") == std::string::npos, "other thread cannot pollute request");
    const auto blocked = root / "not-a-directory";
    { std::ofstream file(blocked); file << "owned fixture"; }
    success(blocked); // 로그 저장 실패는 작업 밖으로 예외를 전달하지 않는다.
    expect(read(blocked) == "owned fixture", "logging failure leaves existing file unchanged");
    nookbit::TimingStage inactive("inactive"); inactive.stop();
    std::filesystem::remove(root / "last-move-timing.log");
    std::filesystem::remove(root / "last-prepare-timing.log");
    std::filesystem::remove(blocked);
    std::filesystem::remove(root);
    return failures ? 1 : 0;
}
