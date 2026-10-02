#include "application/use_cases/create_preview_use_case.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 테스트가 만든 임시 폴더만 마지막에 정리하는 보조 객체.
struct FixtureDirectory {
    std::filesystem::path path;
    bool created{false};

    // 고유한 임시 테스트 폴더를 만드는 생성자.
    FixtureDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("nookbit-planner-test-" + std::to_string(stamp));
        created = std::filesystem::create_directory(path);
    }

    // 자신이 만든 임시 폴더만 삭제하는 소멸자.
    ~FixtureDirectory() {
        if (created && path.parent_path() == std::filesystem::temp_directory_path() &&
            path.filename().string().starts_with("nookbit-planner-test-")) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
};

// 스캐너가 읽을 테스트 파일을 만드는 함수.
bool writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream << contents;
    return stream.good();
}

// jpg 파일에 적용할 단순한 분류 규칙을 만드는 함수.
nookbit::RuleSet imageRules() {
    nookbit::ClassificationRule rule;
    rule.id = "images";
    rule.enabled = true;
    rule.priority = 100;
    rule.extensions = {"jpg"};
    rule.destinationRelativePath = L"Pictures";
    return {"default", "rules-1", {rule}};
}

class FakeSettingsRepository final : public nookbit::ISettingsRepository {
public:
    nookbit::UserSettings settings;

    // 테스트에서 준비한 설정을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::UserSettings> load() const override {
        return nookbit::Result<nookbit::UserSettings>::success(settings);
    }

    // 이 테스트에서 설정을 저장하지 않도록 요청을 거부하는 함수.
    [[nodiscard]] nookbit::Result<void> save(const nookbit::UserSettings&) override {
        return nookbit::Result<void>::failure({"test.save_not_used", "Saving is not used here."});
    }
};

class FakeRuleSetRepository final : public nookbit::IRuleSetRepository {
public:
    nookbit::RuleSet rules = imageRules();

    // 테스트에서 준비한 규칙 묶음을 반환하는 함수.
    [[nodiscard]] nookbit::Result<nookbit::RuleSet> load() const override {
        return nookbit::Result<nookbit::RuleSet>::success(rules);
    }
};

// 선택 파일부터 이동 없는 계획 초안까지 실제 구현이 이어지는지 검사하는 함수.
void testPreviewWorkflowWithoutMoving() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto source = fixture.path / "incoming" / "photo.jpg";
    const auto home = fixture.path / "organized";
    std::filesystem::create_directories(source.parent_path());
    std::filesystem::create_directories(home);
    expect(writeFile(source, "image data"), "source fixture should be created");

    FakeSettingsRepository settings;
    settings.settings.homeRoot = home;
    FakeRuleSetRepository rules;
    nookbit::FilesystemScanner scanner;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, fixture.path / "app-data");
    nookbit::CreatePreviewUseCase useCase(settings, rules, scanner, planner);
    const auto result = useCase.execute({"rules-1", {source}});

    expect(result.hasValue(), "selected file should produce a preview draft");
    if (result) {
        expect(result.value().status == nookbit::PlanStatus::Draft,
               "preview must remain an unapproved draft");
        expect(result.value().planHash.empty(),
               "draft must not pretend to have an approval hash");
        expect(result.value().steps.size() == 1, "one selected image should make one step");
        if (result.value().steps.size() == 1) {
            const auto& step = result.value().steps.front();
            expect(step.source == source, "step should retain the exact selected source");
            expect(step.destination == home / "Pictures" / "photo.jpg",
                   "step should contain the proposed full destination");
            expect(step.matchedRuleId == "images", "step should record the matching rule");
        }
    }
    expect(std::filesystem::exists(source), "preview must leave the source file intact");
    expect(!std::filesystem::exists(home / "Pictures" / "photo.jpg"),
           "preview must not create the destination file");
}

// 기존 파일과 계획 안의 중복 목적지를 안전하게 이름 변경하는지 검사하는 함수.
void testCollisionPolicies() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto first = fixture.path / "one" / "photo.jpg";
    const auto second = fixture.path / "two" / "photo.jpg";
    const auto home = fixture.path / "organized";
    std::filesystem::create_directories(first.parent_path());
    std::filesystem::create_directories(second.parent_path());
    std::filesystem::create_directories(home / "Pictures");
    expect(writeFile(first, "first"), "first source should be created");
    expect(writeFile(second, "second"), "second source should be created");
    expect(writeFile(home / "Pictures" / "photo.jpg", "existing"),
           "existing destination should be created");

    nookbit::FilesystemScanner scanner;
    const auto snapshot = scanner.scan({{}, false, {}, {first, second}});
    expect(snapshot.hasValue(), "both selected files should be scanned");
    if (!snapshot) return;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, fixture.path / "app-data");
    nookbit::UserSettings settings;
    settings.homeRoot = home;
    const auto rules = imageRules();
    const auto renamed = planner.buildPlan(snapshot.value(), settings, rules);
    expect(renamed.hasValue() && renamed.value().steps.size() == 2,
           "rename policy should plan both files");
    if (renamed && renamed.value().steps.size() == 2) {
        expect(renamed.value().steps[0].destination == home / "Pictures" / "photo (1).jpg",
               "first collision should use suffix one");
        expect(renamed.value().steps[1].destination == home / "Pictures" / "photo (2).jpg",
               "second planned destination should reserve suffix two");
    }

    settings.defaultCollisionPolicy = nookbit::CollisionPolicy::Skip;
    const auto skipped = planner.buildPlan(snapshot.value(), settings, rules);
    expect(skipped.hasValue() && skipped.value().steps.size() == 2 &&
               skipped.value().steps[0].status == nookbit::StepStatus::Skipped,
           "skip policy should show a skipped step");

    settings.defaultCollisionPolicy = nookbit::CollisionPolicy::Abort;
    const auto aborted = planner.buildPlan(snapshot.value(), settings, rules);
    expect(!aborted.hasValue() && aborted.error().code == "collision.abort",
           "abort policy should stop the preview on a collision");
    expect(std::filesystem::exists(first) && std::filesystem::exists(second),
           "collision planning must not move source files");
}

// 규칙에 맞지 않는 파일의 처리 방식을 설정대로 적용하는지 검사하는 함수.
void testUnmatchedPolicy() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto source = fixture.path / "notes.txt";
    const auto home = fixture.path / "organized";
    std::filesystem::create_directories(home);
    expect(writeFile(source, "notes"), "unmatched source should be created");
    nookbit::FilesystemScanner scanner;
    const auto snapshot = scanner.scan({{}, false, {}, {source}});
    expect(snapshot.hasValue(), "unmatched file should still be scanned");
    if (!snapshot) return;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, fixture.path / "app-data");
    nookbit::UserSettings settings;
    settings.homeRoot = home;
    const auto rules = imageRules();
    const auto left = planner.buildPlan(snapshot.value(), settings, rules);
    expect(left.hasValue() && left.value().steps.empty() && !left.value().warnings.empty(),
           "leave-in-place should keep an unmatched file out of steps with a warning");
    if (left) {
        expect(left.value().previewOutcomes.size() == 1 &&
                   left.value().previewOutcomes.front().disposition == nookbit::PreviewDisposition::Unmatched &&
                   !left.value().previewOutcomes.front().destination,
               "unmatched file should have a structured non-move preview result");
    }
    settings.unmatchedPolicy = nookbit::UnmatchedPolicy::Abort;
    const auto aborted = planner.buildPlan(snapshot.value(), settings, rules);
    expect(!aborted.hasValue() && aborted.error().code == "plan.unmatched_file",
           "abort policy should reject an unmatched file");
}

// 명시적으로 제외한 파일은 미일치 중단 설정과 무관하게 이동하지 않는지 검사하는 함수.
void testExplicitExclusion() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto source = fixture.path / "private.jpg";
    const auto home = fixture.path / "organized";
    std::filesystem::create_directories(home);
    expect(writeFile(source, "private"), "excluded source fixture should be created");
    nookbit::FilesystemScanner scanner;
    const auto snapshot = scanner.scan({{}, false, {}, {source}});
    expect(snapshot.hasValue(), "excluded file should still be scanned");
    if (!snapshot) return;
    nookbit::RuleSet rules;
    rules.id = "exclusion";
    rules.version = "v3";
    rules.schemaVersion = 3;
    nookbit::RuleNode exclude;
    exclude.id = "skip-jpg";
    exclude.atomicCondition = nookbit::ExtensionPredicate{{"jpg"}};
    exclude.exclude = true;
    rules.nodes = {exclude};
    rules.globalOtherwise = nookbit::RuleDestination{L"Fallback", std::nullopt};
    nookbit::UserSettings settings;
    settings.homeRoot = home;
    settings.unmatchedPolicy = nookbit::UnmatchedPolicy::Abort;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, fixture.path / "app-data");
    const auto plan = planner.buildPlan(snapshot.value(), settings, rules);
    expect(plan && plan.value().steps.empty() && !plan.value().warnings.empty() &&
               plan.value().warnings.front().code == "plan.excluded_file",
           "explicit exclusion should skip movement without triggering unmatched abort");
    if (plan) {
        expect(plan.value().previewOutcomes.size() == 1 &&
                   plan.value().previewOutcomes.front().disposition == nookbit::PreviewDisposition::Excluded &&
                   plan.value().previewOutcomes.front().matchedRuleId == "skip-jpg",
               "excluded file should identify the rule without creating a move step");
    }
    expect(std::filesystem::exists(source), "excluded source must remain untouched");
}

// 앱 데이터 폴더의 파일을 선택해도 이동 초안을 만들지 않는지 검사하는 함수.
void testAppDataIsProtected() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto appData = fixture.path / "app-data";
    const auto source = appData / "private.jpg";
    const auto home = fixture.path / "organized";
    std::filesystem::create_directories(appData);
    std::filesystem::create_directories(home);
    expect(writeFile(source, "private"), "app-data fixture should be created");
    nookbit::FilesystemScanner scanner;
    const auto snapshot = scanner.scan({{}, false, {}, {source}});
    expect(snapshot.hasValue(), "selected app-data file can be read for validation");
    if (!snapshot) return;
    nookbit::RuleEngine engine;
    nookbit::Planner planner(engine, appData);
    nookbit::UserSettings settings;
    settings.homeRoot = home;
    const auto result = planner.buildPlan(snapshot.value(), settings, imageRules());
    expect(!result.hasValue() && result.error().code == "path.app_data_protected",
           "planner must reject a source inside protected app data");
    expect(std::filesystem::exists(source), "rejected source should remain untouched");
}

} // namespace

// Planner와 실제 미리보기 흐름의 테스트를 실행하는 함수.
int main() {
    testPreviewWorkflowWithoutMoving();
    testCollisionPolicies();
    testUnmatchedPolicy();
    testExplicitExclusion();
    testAppDataIsProtected();
    if (failures == 0) {
        std::cout << "All planner tests passed.\n";
        return 0;
    }
    std::cerr << failures << " planner assertion(s) failed.\n";
    return 1;
}
