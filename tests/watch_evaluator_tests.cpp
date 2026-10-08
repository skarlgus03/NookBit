#include "modules/watch/watch_evaluator.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "modules/watch/watch_file_readiness.hpp"

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
    nookbit::EvaluateWatchFolderUseCase useCase(scanner, rulesEngine, rules, 0);
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
    const auto pendingPhoto = useCase.execute(settings, 60000,
        {L"C:/fake/Downloads/./photo.jpg"});
    expect(pendingPhoto.hasValue() && pendingPhoto.value().eligibleFiles.empty() &&
               !pendingPhoto.value().evaluation.shouldOffer &&
               pendingPhoto.value().evaluation.fileCount == 0 &&
               pendingPhoto.value().evaluation.totalBytes == 0 &&
               pendingPhoto.value().evaluation.oldestAgeMinutes == 0 &&
               pendingPhoto.value().warnings.size() == 1,
           "pending files must not contribute to count, size, age or proposal paths");
#ifdef _WIN32
    const auto differentCase = useCase.execute(settings, 60000,
        {L"c:\\FAKE\\downloads\\PHOTO.JPG"});
    expect(differentCase.hasValue() && differentCase.value().eligibleFiles.empty(),
           "Windows pending path comparison must ignore case and slash differences");
#endif
    const auto unrelatedPending = useCase.execute(settings, 60000,
        {L"C:/another/photo.jpg"});
    expect(unrelatedPending.hasValue() && unrelatedPending.value().evaluation.shouldOffer,
           "a same-named pending file in another folder must not suppress the candidate");
    scanner.fail = true;
    const auto failed = useCase.execute(settings, 60000);
    expect(!failed.hasValue() && failed.error().code == "test.scan_failed",
           "watch use case should return scanner failures unchanged");
}

// 관찰 시각을 직접 넘겨 다운로드 대기와 변경·재등장 경계값을 검사하는 함수.
void testFileReadiness() {
    nookbit::WatchFileReadiness readiness;
    auto file = fact(100, 0);
    file.absolutePath = L"C:/downloads/photo.jpg";
    file.fingerprint.fileId = "first";
    auto first = readiness.observe({file}, 100000);
    expect(first.readyFiles.empty() && first.waitingCount == 1 && first.nextCheckDelayMs == 30000,
           "even an old file must be observed before it can be offered");
    auto early = readiness.observe({file}, 129999);
    expect(early.readyFiles.empty() && early.nextCheckDelayMs == 1,
           "unchanged files must wait until the stable window boundary");
    auto ready = readiness.observe({file}, 130000);
    expect(ready.readyFiles.size() == 1 && ready.waitingCount == 0 && ready.nextCheckDelayMs == 0,
           "an unchanged file should become ready at thirty seconds with no further polling");
    file.fingerprint.size = 101;
    auto grew = readiness.observe({file}, 140000);
    expect(grew.readyFiles.empty() && grew.nextCheckDelayMs == 30000,
           "a growing file must start a new stable window");
    file.fingerprint.modifiedUtcMs = 150000;
    auto rewritten = readiness.observe({file}, 150000);
    expect(rewritten.readyFiles.empty() && rewritten.nextCheckDelayMs == 30000,
           "a same-size rewrite must restart the wait");
    file.fingerprint.fileId = "replacement";
    auto replaced = readiness.observe({file}, 180000);
    expect(replaced.readyFiles.empty(), "replacement at the same path must restart the wait");
    expect(readiness.observe({file}, 210000).readyFiles.size() == 1,
           "the replacement should become ready after its own stable window");
    expect(readiness.observe({}, 220000).nextCheckDelayMs == 0,
           "disappeared files must stop readiness retries");
    expect(readiness.observe({file}, 250000).readyFiles.empty(),
           "reappearing files must not reuse a disappeared observation");
    expect(readiness.observe({file}, 240000).nextCheckDelayMs == 30000,
           "a backwards clock must reset observations instead of accepting early");
    auto temporary = file;
    for (const auto* name : {L"photo.jpg.CRDOWNLOAD", L"photo.jpg.part", L"photo.jpg.partial", L"photo.jpg.download"}) {
        temporary.absolutePath = std::filesystem::path(L"C:/downloads") / name;
        readiness.reset();
        expect(readiness.observe({temporary}, 300000).nextCheckDelayMs == 0 &&
                   readiness.observe({temporary}, 400000).readyFiles.empty(),
               "download temporary names must stay excluded even when unchanged");
    }
    expect(readiness.observe({file}, 400000).readyFiles.empty(),
           "renaming a temporary download to its final name must begin observation");
}

// 안정 대기 중인 파일이 조건 계산에 들어가지 않고 검사 실패 후 새로 관찰되는지 검사하는 함수.
void testStableWatchUseCase() {
    FakeScanner scanner;
    FakeRuleEngine engine;
    FakeRules rules;
    nookbit::EvaluateWatchFolderUseCase useCase(scanner, engine, rules);
    nookbit::WatchSettings settings;
    settings.folder = L"C:/fake/Downloads";
    settings.conditions = {{nookbit::WatchConditionType::FileCount, 1}};
    const auto waiting = useCase.execute(settings, 100000);
    expect(waiting && !waiting.value().evaluation.shouldOffer &&
               waiting.value().evaluation.fileCount == 0 && waiting.value().evaluation.totalBytes == 0 &&
               waiting.value().waitingFileCount == 1 && waiting.value().nextCheckDelayMs == 30000,
           "unstable classified files must not contribute to watch conditions");
    const auto ready = useCase.execute(settings, 130000);
    expect(ready && ready.value().evaluation.shouldOffer && ready.value().eligibleFiles.size() == 1,
           "stable classified files should trigger normal watch conditions");
    scanner.fail = true;
    expect(!useCase.execute(settings, 140000), "scanner failure must propagate");
    scanner.fail = false;
    const auto afterFailure = useCase.execute(settings, 170000);
    expect(afterFailure && !afterFailure.value().evaluation.shouldOffer,
           "a failed scan must invalidate earlier stable observations");
    settings.folder = L"C:/other";
    const auto switched = useCase.execute(settings, 200000);
    expect(switched && !switched.value().evaluation.shouldOffer,
           "switching watched folders must reset observations");
}

} // namespace

// 감시 조건 판단 테스트를 실행하는 함수.
int main() {
    testConditions();
    testWatchUseCase();
    testFileReadiness();
    testStableWatchUseCase();
    if (failures == 0) {
        std::cout << "All watch evaluator tests passed.\n";
        return 0;
    }
    std::cerr << failures << " watch evaluator assertion(s) failed.\n";
    return 1;
}
