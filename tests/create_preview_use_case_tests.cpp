#include "application/use_cases/create_preview_use_case.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;
const std::vector<std::filesystem::path> sampleSelection{L"C:/watch/file.txt"};

// 조건이 거짓일 때 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class FakeSettingsRepository final : public nookbit::ISettingsRepository {
public:
    nookbit::Result<nookbit::UserSettings> loadResult =
        nookbit::Result<nookbit::UserSettings>::success({});
    mutable int loadCalls{0};

    // 준비된 설정 결과를 반환하는 테스트용 함수.
    [[nodiscard]] nookbit::Result<nookbit::UserSettings> load() const override {
        ++loadCalls;
        return loadResult;
    }

    // 테스트에서는 설정을 저장하지 않고 성공만 반환하는 함수.
    [[nodiscard]] nookbit::Result<void> save(
        const nookbit::UserSettings&) override {
        return nookbit::Result<void>::success();
    }
};

class FakeRuleSetRepository final : public nookbit::IRuleSetRepository {
public:
    nookbit::Result<nookbit::RuleSet> loadResult =
        nookbit::Result<nookbit::RuleSet>::success({"default", "rules-1", {}});
    mutable int loadCalls{0};

    // 준비된 분류 규칙을 반환하고 호출 횟수를 기록하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::RuleSet> load() const override {
        ++loadCalls;
        return loadResult;
    }
};

class FakeScanner final : public nookbit::IScanner {
public:
    nookbit::Result<nookbit::ScanSnapshot> scanResult =
        nookbit::Result<nookbit::ScanSnapshot>::success({});
    nookbit::ScanRequest lastRequest;
    int scanCalls{0};

    // 전달받은 요청을 기록하고 준비된 스캔 결과를 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::ScanSnapshot> scan(
        const nookbit::ScanRequest& request) override {
        ++scanCalls;
        lastRequest = request;
        return scanResult;
    }
};

class FakePlanner final : public nookbit::IPlanner {
public:
    nookbit::Result<nookbit::OperationPlan> planResult =
        nookbit::Result<nookbit::OperationPlan>::success({});
    std::string lastSnapshotId;
    nookbit::RuleSetVersion lastRuleSetVersion;
    std::filesystem::path lastHomeRoot;
    int buildCalls{0};

    // 전달받은 입력을 기록하고 준비된 이동 계획을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::OperationPlan> buildPlan(
        const nookbit::ScanSnapshot& snapshot,
        const nookbit::UserSettings& settings,
        const nookbit::RuleSet& rules) override {
        ++buildCalls;
        lastSnapshotId = snapshot.id;
        lastRuleSetVersion = rules.version;
        lastHomeRoot = settings.homeRoot;
        return planResult;
    }
};

// 설정, 스캔, 계획 생성이 올바른 입력으로 순서대로 호출되는지 검사하는 함수.
void testSuccessfulPreview() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;

    nookbit::UserSettings userSettings;
    userSettings.homeRoot = L"C:/organized";
    userSettings.watchRoots = {L"C:/watch"};
    userSettings.excludePatterns = {"*.partial"};
    userSettings.recursiveScan = true;
    settings.loadResult =
        nookbit::Result<nookbit::UserSettings>::success(userSettings);

    nookbit::ScanSnapshot snapshot;
    snapshot.id = "snapshot-1";
    scanner.scanResult =
        nookbit::Result<nookbit::ScanSnapshot>::success(snapshot);

    nookbit::OperationPlan plan;
    plan.id = "plan-1";
    plan.status = nookbit::PlanStatus::Planned;
    planner.planResult =
        nookbit::Result<nookbit::OperationPlan>::success(plan);

    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);
    const auto result = useCase.execute({"rules-1", sampleSelection});

    expect(result.hasValue(), "preview should return the generated plan");
    if (result) {
        expect(result.value().id == "plan-1", "preview should preserve the plan result");
    }
    expect(settings.loadCalls == 1, "preview should load settings once");
    expect(rules.loadCalls == 1, "preview should load rules once");
    expect(scanner.scanCalls == 1, "preview should scan once");
    expect(planner.buildCalls == 1, "preview should build one plan");
    expect(scanner.lastRequest.selectedFiles == sampleSelection,
           "manual preview should scan only selected files");
    expect(scanner.lastRequest.roots.empty(),
           "manual preview should not scan configured watch folders");
    expect(planner.lastSnapshotId == "snapshot-1",
           "preview should pass the scan snapshot to the planner");
    expect(planner.lastRuleSetVersion == "rules-1",
           "preview should pass the requested rule-set version to the planner");
}

// 화면 요청의 목적지를 사용하고 규칙을 한 번만 읽는지 검사하는 함수.
void testRequestDestinationLoadsRulesOnce() {
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;
    nookbit::CreatePreviewUseCase useCase(rules, scanner, planner);

    const auto result = useCase.execute({{}, sampleSelection, L"C:/requested-home"});

    expect(result.hasValue(), "request destination should create a preview");
    expect(rules.loadCalls == 1, "preview should load current rules once");
    expect(planner.lastHomeRoot == std::filesystem::path(L"C:/requested-home"),
           "planner should receive the requested destination");
    expect(planner.lastRuleSetVersion == "rules-1",
           "planner should use the loaded rules version");
}

// 설정 저장소가 없을 때 목적지를 빠뜨린 요청을 거절하는지 검사하는 함수.
void testRequestWithoutDestinationStopsEarly() {
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;
    nookbit::CreatePreviewUseCase useCase(rules, scanner, planner);

    const auto result = useCase.execute({"rules-1", sampleSelection});

    expect(!result.hasValue() &&
               result.error().code == "preview.destination_root_empty",
           "preview without settings should require a destination");
    expect(rules.loadCalls == 0 && scanner.scanCalls == 0 && planner.buildCalls == 0,
           "missing destination should stop before calling workers");
}

// 잘못된 요청이 외부 모듈을 호출하기 전에 거부되는지 검사하는 함수.
void testEmptyRuleSetVersionStopsEarly() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({});

    expect(!result.hasValue() &&
               result.error().code == "preview.rule_set_version_empty",
           "preview should reject an empty rule-set version");
    expect(settings.loadCalls == 0, "invalid input should not load settings");
    expect(rules.loadCalls == 0, "invalid input should not load rules");
    expect(scanner.scanCalls == 0, "invalid input should not scan files");
    expect(planner.buildCalls == 0, "invalid input should not build a plan");
}

// 설정 오류가 그대로 반환되고 이후 단계가 중단되는지 검사하는 함수.
void testSettingsFailureStopsWorkflow() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;
    settings.loadResult = nookbit::Result<nookbit::UserSettings>::failure({
        "settings.load_failed",
        "Settings could not be loaded.",
    });
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({"rules-1", sampleSelection});

    expect(!result.hasValue() && result.error().code == "settings.load_failed",
           "preview should preserve a settings error");
    expect(scanner.scanCalls == 0, "settings failure should stop before scanning");
    expect(rules.loadCalls == 0, "settings failure should stop before rules");
    expect(planner.buildCalls == 0, "settings failure should stop before planning");
}

// 대기 파일이 없을 때 스캔을 시작하지 않는지 검사하는 함수.
void testEmptySelectionStopsBeforeScanning() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;

    nookbit::UserSettings userSettings;
    userSettings.homeRoot = L"C:/organized";
    settings.loadResult =
        nookbit::Result<nookbit::UserSettings>::success(userSettings);
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({"rules-1", {}});

    expect(!result.hasValue() &&
               result.error().code == "preview.selection_empty",
           "preview should reject an empty pending list");
    expect(settings.loadCalls == 0, "empty selection should stop before settings");
    expect(scanner.scanCalls == 0, "empty selection should stop before scanning");
    expect(rules.loadCalls == 0, "empty selection should stop before rules");
    expect(planner.buildCalls == 0, "empty selection should stop before planning");
}

// 규칙 로드 오류가 스캔 전에 반환되는지 검사하는 함수.
void testRuleLoadFailureStopsBeforeScanning() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;
    nookbit::UserSettings userSettings;
    userSettings.watchRoots = {L"C:/watch"};
    settings.loadResult = nookbit::Result<nookbit::UserSettings>::success(userSettings);
    rules.loadResult = nookbit::Result<nookbit::RuleSet>::failure({
        "rules.load_failed", "Rules could not be loaded."});
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({"rules-1", sampleSelection});

    expect(!result.hasValue() && result.error().code == "rules.load_failed",
           "preview should preserve a rules error");
    expect(scanner.scanCalls == 0, "rules failure should stop before scanning");
    expect(planner.buildCalls == 0, "rules failure should stop before planning");
}

// 화면이 요청한 규칙 버전과 파일 버전이 다르면 스캔하지 않는지 검사하는 함수.
void testRuleVersionMismatchStopsBeforeScanning() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;
    nookbit::UserSettings userSettings;
    userSettings.watchRoots = {L"C:/watch"};
    settings.loadResult = nookbit::Result<nookbit::UserSettings>::success(userSettings);
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({"different-version", sampleSelection});

    expect(!result.hasValue() &&
               result.error().code == "preview.rule_set_version_mismatch",
           "preview should reject a stale rules version");
    expect(scanner.scanCalls == 0, "version mismatch should stop before scanning");
    expect(planner.buildCalls == 0, "version mismatch should stop before planning");
}

// 스캔 오류가 그대로 반환되고 계획 생성이 중단되는지 검사하는 함수.
void testScanFailureStopsWorkflow() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;

    nookbit::UserSettings userSettings;
    userSettings.homeRoot = L"C:/organized";
    userSettings.watchRoots = {L"C:/watch"};
    settings.loadResult =
        nookbit::Result<nookbit::UserSettings>::success(userSettings);
    scanner.scanResult = nookbit::Result<nookbit::ScanSnapshot>::failure({
        "scan.failed",
        "The watch root could not be scanned.",
    });
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({"rules-1", sampleSelection});

    expect(!result.hasValue() && result.error().code == "scan.failed",
           "preview should preserve a scanner error");
    expect(planner.buildCalls == 0, "scan failure should stop before planning");
}

// 계획 생성 오류가 유스케이스 호출자에게 그대로 전달되는지 검사하는 함수.
void testPlannerFailureIsReturned() {
    FakeSettingsRepository settings;
    FakeRuleSetRepository rules;
    FakeScanner scanner;
    FakePlanner planner;

    nookbit::UserSettings userSettings;
    userSettings.homeRoot = L"C:/organized";
    userSettings.watchRoots = {L"C:/watch"};
    settings.loadResult =
        nookbit::Result<nookbit::UserSettings>::success(userSettings);
    planner.planResult = nookbit::Result<nookbit::OperationPlan>::failure({
        "plan.failed",
        "The preview plan could not be created.",
    });
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);

    const auto result = useCase.execute({"rules-1", sampleSelection});

    expect(!result.hasValue() && result.error().code == "plan.failed",
           "preview should preserve a planner error");
    expect(planner.buildCalls == 1, "valid scan results should reach the planner");
}

// 모든 미리보기 유스케이스 테스트를 실행하고 결과 코드를 반환하는 함수.
int runTests() {
    testSuccessfulPreview();
    testRequestDestinationLoadsRulesOnce();
    testRequestWithoutDestinationStopsEarly();
    testEmptyRuleSetVersionStopsEarly();
    testSettingsFailureStopsWorkflow();
    testEmptySelectionStopsBeforeScanning();
    testRuleLoadFailureStopsBeforeScanning();
    testRuleVersionMismatchStopsBeforeScanning();
    testScanFailureStopsWorkflow();
    testPlannerFailureIsReturned();

    if (failures == 0) {
        std::cout << "All preview use-case tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}

} // namespace

// 미리보기 유스케이스 테스트 실행을 시작하는 함수.
int main() {
    return runTests();
}
