#include "modules/watch/watch_evaluator.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"

#include <iostream>
#include <vector>

namespace {

int failures = 0;

// 예상한 판단 결과와 다르면 테스트 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 테스트용 파일 정보를 지정한 크기와 경과 시간으로 만드는 함수.
nookbit::FileFact fact(std::uintmax_t size, std::int64_t modifiedMs) {
    nookbit::FileFact file;
    file.fingerprint.size = size;
    file.fingerprint.modifiedUtcMs = modifiedMs;
    return file;
}

/// 호출 요청을 보관하고 가짜 파일 정보를 돌려주는 Scanner.
class FakeScanner final : public nookbit::IScanner {
public:
    nookbit::ScanRequest lastRequest;
    bool fail{false};

    // 감시 요청을 기록하고 테스트용 파일 두 개를 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::ScanSnapshot> scan(
        const nookbit::ScanRequest& request) override {
        lastRequest = request;
        if (fail) return nookbit::Result<nookbit::ScanSnapshot>::failure({
            "test.scan_failed", "Injected scan failure.",
        });
        nookbit::ScanSnapshot result;
        auto photo = fact(50, 0);
        photo.absolutePath = L"C:/fake/Downloads/photo.jpg";
        photo.extension = ".jpg";
        auto note = fact(30, 0);
        note.absolutePath = L"C:/fake/Downloads/note.txt";
        note.extension = ".txt";
        result.files = {photo, note};
        result.warnings.push_back({"test.warning", "One entry was skipped."});
        return nookbit::Result<nookbit::ScanSnapshot>::success(std::move(result));
    }
};

/// 이미지 파일만 분류된 것으로 돌려주는 규칙 판별기.
class FakeRuleEngine final : public nookbit::IRuleEngine {
public:
    // 테스트용 JPG 파일에만 분류 결정을 반환하는 함수.
    [[nodiscard]] nookbit::Result<std::optional<nookbit::RuleDecision>> decide(
        const nookbit::FileFact& file, const nookbit::RuleSet&,
        nookbit::MultiMatchPolicy, std::int64_t) const override {
        if (file.extension != ".jpg") {
            return nookbit::Result<std::optional<nookbit::RuleDecision>>::success(std::nullopt);
        }
        return nookbit::Result<std::optional<nookbit::RuleDecision>>::success(
            nookbit::RuleDecision{"photos", L"Pictures", "Matched", std::nullopt});
    }
};

/// 테스트용 규칙 묶음을 제공하는 저장소.
class FakeRules final : public nookbit::IRuleSetRepository {
public:
    // 외부 파일 없이 테스트 규칙 묶음을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::RuleSet> load() const override {
        return nookbit::Result<nookbit::RuleSet>::success({"test", "1"});
    }
};

// 사용자가 고른 조건 조합과 경계값을 검사하는 함수.
void testConditions() {
    constexpr std::int64_t nowMs = 600000;
    nookbit::WatchSettings settings;
    settings.conditions = {
        {nookbit::WatchConditionType::FileCount, 2},
        {nookbit::WatchConditionType::TotalSizeMiB, 2},
        {nookbit::WatchConditionType::OldestAgeMinutes, 5},
    };
    const std::vector<nookbit::FileFact> files = {
        fact(1024 * 1024, nowMs - 4 * 60000),
        fact(1024 * 1024, nowMs - 5 * 60000),
    };
    settings.combination = nookbit::WatchCombination::All;
    const auto all = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(all.hasValue() && all.value().shouldOffer && all.value().fileCount == 2 &&
               all.value().totalBytes == 2 * 1024 * 1024 && all.value().oldestAgeMinutes == 5,
           "all conditions should match exactly at their thresholds");
    settings.conditions[1].threshold = 3;
    const auto notAll = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(notAll.hasValue() && !notAll.value().shouldOffer,
           "all combination should reject one unmet condition");
    settings.combination = nookbit::WatchCombination::Any;
    const auto any = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(any.hasValue() && any.value().shouldOffer,
           "any combination should accept one met condition");
    const auto empty = nookbit::WatchEvaluator::evaluate(settings, {}, nowMs);
    expect(empty.hasValue() && !empty.value().shouldOffer,
           "empty folder must never trigger a proposal");
    settings.conditions.push_back(settings.conditions.front());
    const auto duplicate = nookbit::WatchEvaluator::evaluate(settings, files, nowMs);
    expect(!duplicate.hasValue(), "duplicate conditions should be rejected");
}

// 유스케이스가 포트로만 스캔·규칙 조회·판단을 요청하는지 검사하는 함수.
void testWatchUseCase() {
    FakeScanner scanner;
    FakeRuleEngine rulesEngine;
    FakeRules rules;
    nookbit::EvaluateWatchFolderUseCase useCase(scanner, rulesEngine, rules);
    nookbit::WatchSettings settings;
    settings.folder = L"C:/fake/Downloads";
    settings.recursive = true;
    settings.excludePatterns = {"*.partial"};
    settings.conditions = {{nookbit::WatchConditionType::FileCount, 1}};
    const auto result = useCase.execute(settings, 60000);
    expect(result.hasValue() && result.value().evaluation.shouldOffer &&
               result.value().eligibleFiles.size() == 1 && result.value().warnings.size() == 1,
           "watch use case should offer only classified files and preserve scan warnings");
    expect(scanner.lastRequest.roots.size() == 1 && scanner.lastRequest.recursive &&
               scanner.lastRequest.excludePatterns.size() == 1 &&
               scanner.lastRequest.selectedFiles.empty(),
           "watch use case should pass folder options through the scanner port");
    scanner.fail = true;
    const auto failed = useCase.execute(settings, 60000);
    expect(!failed.hasValue() && failed.error().code == "test.scan_failed",
           "watch use case should return scanner failures unchanged");
}

} // namespace

// 감시 조건 판단 테스트를 실행하는 함수.
int main() {
    testConditions();
    testWatchUseCase();
    if (failures == 0) {
        std::cout << "All watch evaluator tests passed.\n";
        return 0;
    }
    std::cerr << failures << " watch evaluator assertion(s) failed.\n";
    return 1;
}
