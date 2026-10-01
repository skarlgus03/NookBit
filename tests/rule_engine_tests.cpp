#include "modules/rules/rule_engine.hpp"
#include "application/use_cases/plan_signature.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;

// 조건이 거짓이면 테스트 실패를 출력하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 테스트에서 사용할 날짜의 UTC 밀리초를 계산하는 함수.
std::int64_t utcMillis(int year, unsigned month, unsigned day) {
    using namespace std::chrono;
    return duration_cast<milliseconds>(sys_days{std::chrono::year{year} / month / day}
                                        .time_since_epoch()).count();
}

// 공통 파일 정보를 만드는 함수.
nookbit::FileFact sampleFile() {
    nookbit::FileFact file;
    file.absolutePath = L"C:/Watch/PHOTO.JPG";
    file.relativePath = L"PHOTO.JPG";
    file.extension = ".JPG";
    file.fingerprint.size = 2048;
    file.fingerprint.modifiedUtcMs = utcMillis(2026, 9, 23);
    return file;
}

// 우선순위, 날짜 목적지, 충돌 정책을 포함한 분류 결과를 검사하는 함수.
void testPriorityAndDestination() {
    nookbit::ClassificationRule low;
    low.id = "generic";
    low.enabled = true;
    low.priority = 10;
    low.extensions = {"jpg"};
    low.destinationRelativePath = L"Generic";

    nookbit::ClassificationRule high = low;
    high.id = "photos";
    high.priority = 20;
    high.fileNameGlobs = {"photo.*"};
    high.destinationRelativePath = L"Pictures/{yyyy}/{MM}";
    high.collisionPolicy = nookbit::CollisionPolicy::Skip;

    nookbit::RuleSet rules{"default", "version-1", {low, high}};
    nookbit::RuleEngine engine;
    const auto result = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(result.hasValue() && result.value().has_value(), "image should match a rule");
    if (result && result.value()) {
        expect(result.value()->matchedRuleId == "photos", "higher priority should win");
        expect(result.value()->destinationRelativePath == L"Pictures/2026/09",
               "date tokens should use the file modification date");
        expect(result.value()->collisionPolicy == nookbit::CollisionPolicy::Skip,
               "rule collision override should reach the decision");
    }

    const auto aborted = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::Abort, utcMillis(2026, 9, 25));
    expect(!aborted.hasValue() && aborted.error().code == "rules.multiple_matches",
           "abort policy should reject multiple matches");

    rules.rules[1].priority = rules.rules[0].priority;
    const auto tied = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(tied.hasValue() && tied.value().has_value() &&
               tied.value()->matchedRuleId == "generic",
           "equal priorities should preserve JSON rule order");
}

// 조건 그룹과 일치하지 않는 파일이 분류되지 않는지 검사하는 함수.
void testConditionsAndNoMatch() {
    nookbit::ClassificationRule rule;
    rule.id = "archive";
    rule.enabled = true;
    rule.priority = 1;
    rule.extensions = {"jpg"};
    rule.fileNameGlobs = {"photo.*"};
    rule.minimumBytes = 1024;
    rule.maximumBytes = 4096;
    rule.minimumAgeDays = 2;
    rule.maximumAgeDays = 4;
    rule.destinationRelativePath = L"Archive";
    nookbit::RuleSet rules{"default", "version-1", {rule}};
    nookbit::RuleEngine engine;

    const auto matched = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(matched.hasValue() && matched.value().has_value(),
           "all matching conditions should produce a decision");

    auto small = sampleFile();
    small.fingerprint.size = 100;
    const auto sizeMiss = engine.decide(small, rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(sizeMiss.hasValue() && !sizeMiss.value().has_value(),
           "a failed size condition should leave the file unclassified");

    const auto ageMiss = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 24));
    expect(ageMiss.hasValue() && !ageMiss.value().has_value(),
           "a failed age condition should leave the file unclassified");

    rules.rules[0].enabled = false;
    const auto disabled = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(disabled.hasValue() && !disabled.value().has_value(),
           "disabled rules should not match");
}

// 파일에 쓰이지 않을 안전하지 않은 목적지를 거부하는지 검사하는 함수.
void testUnsafeDestination() {
    nookbit::ClassificationRule rule;
    rule.id = "unsafe";
    rule.enabled = true;
    rule.destinationRelativePath = L"../Escape";
    nookbit::RuleSet rules{"default", "version-1", {rule}};
    nookbit::RuleEngine engine;
    const auto result = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(!result.hasValue() && result.error().code == "rules.unsafe_destination",
           "parent-path destination should be rejected");

    rules.rules[0].destinationRelativePath = L"Pictures/{unknown}";
    const auto token = engine.decide(sampleFile(), rules,
        nookbit::MultiMatchPolicy::FirstMatch, utcMillis(2026, 9, 25));
    expect(!token.hasValue() && token.error().code == "rules.unknown_destination_token",
           "unsupported destination token should be rejected");
}

// 트리의 첫 일치·중첩 조건·else·전체 예외 동작을 검사하는 함수.
void testOrderedTreeAndOtherwise() {
    nookbit::RuleSet rules;
    rules.id = "tree";
    rules.version = "v2";
    rules.schemaVersion = 2;
    nookbit::RuleNode large;
    large.id = "large";
    large.condition.greaterThanBytes = 20 * 1024 * 1024;
    large.destination = nookbit::RuleDestination{L"Large", std::nullopt};
    nookbit::RuleNode image;
    image.id = "image";
    image.condition.extensions = {"jpg", "png"};
    nookbit::RuleNode png;
    png.id = "png";
    png.condition.extensions = {"png"};
    png.destination = nookbit::RuleDestination{L"Pictures/PNG", std::nullopt};
    nookbit::RuleNode other;
    other.id = "other-image";
    other.otherwise = true;
    other.destination = nookbit::RuleDestination{L"Pictures/Other", std::nullopt};
    image.children = {png, other};
    rules.nodes = {large, image};
    nookbit::RuleEngine engine;
    auto file = sampleFile();
    file.extension = "png";
    file.fingerprint.size = 21 * 1024 * 1024;
    auto decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::Abort,
                                  utcMillis(2026, 9, 25));
    expect(decision && decision.value() && decision.value()->matchedRuleId == "large",
           "first matching root must win even when another branch also matches");
    file.fingerprint.size = 5 * 1024 * 1024;
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::Abort,
                             utcMillis(2026, 9, 25));
    expect(decision && decision.value() && decision.value()->matchedRuleId == "png",
           "matching child should provide the destination");
    file.extension = "jpg";
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::Abort,
                             utcMillis(2026, 9, 25));
    expect(decision && decision.value() && decision.value()->matchedRuleId == "other-image",
           "else child should catch unmatched siblings at its own level");
    image.children.pop_back();
    rules.nodes[1] = image;
    nookbit::RuleNode laterRoot;
    laterRoot.id = "later-root";
    laterRoot.condition.fileNameGlobs = {"PHOTO.*"};
    laterRoot.destination = nookbit::RuleDestination{L"Later", std::nullopt};
    rules.nodes.push_back(laterRoot);
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::Abort,
                             utcMillis(2026, 9, 25));
    expect(decision && !decision.value(),
           "a matching branch with no leaf must not backtrack to a later root");
    rules.globalOtherwise = nookbit::RuleDestination{L"Unsorted", std::nullopt};
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::Abort,
                             utcMillis(2026, 9, 25));
    expect(decision && decision.value() &&
               decision.value()->matchedRuleId == "global-otherwise",
           "global else should catch a branch with no destination");
    const auto originalSignature = nookbit::PlanSignature::rules(rules);
    std::swap(rules.nodes[0], rules.nodes[1]);
    expect(nookbit::PlanSignature::rules(rules) != originalSignature,
           "changing sibling order must invalidate saved plans");
}

// v3의 단일 조건과 선택 경로를 자식 경로의 AND로 평가하는지 검사하는 함수.
void testAtomicConditionTree() {
    nookbit::RuleSet rules;
    rules.id = "atomic";
    rules.version = "v3";
    rules.schemaVersion = 3;
    nookbit::RuleNode large;
    large.id = "larger-than-five";
    large.atomicCondition = nookbit::SizePredicate{
        nookbit::SizeComparison::GreaterThan, 5 * 1024 * 1024};
    nookbit::RuleNode image;
    image.id = "image-extension";
    image.atomicCondition = nookbit::ExtensionPredicate{{"png", "jpg"}};
    image.destination = nookbit::RuleDestination{L"Pictures", std::nullopt};
    large.children = {image};
    nookbit::RuleNode name;
    name.id = "named-report";
    name.atomicCondition = nookbit::NameContainsPredicate{"report"};
    name.destination = nookbit::RuleDestination{L"Reports", std::nullopt};
    rules.nodes = {large, name};

    nookbit::RuleEngine engine;
    auto file = sampleFile();
    file.absolutePath = L"C:/Input/Photo.PNG";
    file.relativePath = L"Photo.PNG";
    file.extension = "PNG";
    file.fingerprint.size = 5 * 1024 * 1024;
    auto decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::FirstMatch,
                                  utcMillis(2026, 9, 29));
    expect(decision && !decision.value(), "greater-than must exclude exact threshold");
    rules.nodes[0].atomicCondition = nookbit::SizePredicate{
        nookbit::SizeComparison::AtLeast, 5 * 1024 * 1024};
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::FirstMatch,
                             utcMillis(2026, 9, 29));
    expect(decision && decision.value() &&
               decision.value()->matchedRuleId == "image-extension",
           "size and extension along one path must both match");
    auto trace = engine.trace(file, rules, utcMillis(2026, 9, 29));
    expect(trace && trace.value().selectedNodeIds ==
               std::vector<std::string>{"larger-than-five", "image-extension"} &&
               trace.value().decision && trace.value().decision->matchedRuleId ==
                   decision.value()->matchedRuleId,
           "rule test trace should match the production decision and selected path");
    file.extension = "exe";
    file.relativePath = L"report.exe";
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::FirstMatch,
                             utcMillis(2026, 9, 29));
    expect(decision && !decision.value(),
           "matching parent must not backtrack to later name sibling");
    trace = engine.trace(file, rules, utcMillis(2026, 9, 29));
    expect(trace && !trace.value().decision &&
               trace.value().selectedNodeIds == std::vector<std::string>{"larger-than-five"},
           "trace should retain the selected parent without backtracking");
    rules.nodes[0].enabled = false;
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::FirstMatch,
                             utcMillis(2026, 9, 29));
    expect(decision && decision.value() && decision.value()->matchedRuleId == "named-report",
           "name-contains condition must match a root when earlier root is disabled");
    nookbit::RuleNode age;
    age.id = "old-files";
    age.atomicCondition = nookbit::ModifiedAgePredicate{nookbit::AgeComparison::AtLeast, 7};
    age.destination = nookbit::RuleDestination{L"Old", std::nullopt};
    rules.nodes = {age};
    file.fingerprint.modifiedUtcMs = utcMillis(2026, 9, 20);
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::FirstMatch,
                             utcMillis(2026, 9, 29));
    expect(decision && decision.value() && decision.value()->matchedRuleId == "old-files",
           "modified-age condition must match elapsed whole days");
    const auto signature = nookbit::PlanSignature::rules(rules);
    std::get<nookbit::ModifiedAgePredicate>(*rules.nodes[0].atomicCondition).days = 8;
    expect(nookbit::PlanSignature::rules(rules) != signature,
           "changing a v3 condition must invalidate a saved plan");
    nookbit::RuleNode excluded;
    excluded.id = "skip-png";
    excluded.atomicCondition = nookbit::ExtensionPredicate{{"png"}};
    excluded.exclude = true;
    rules.nodes = {excluded};
    rules.globalOtherwise = nookbit::RuleDestination{L"Fallback", std::nullopt};
    file.extension = "png";
    decision = engine.decide(file, rules, nookbit::MultiMatchPolicy::FirstMatch,
                             utcMillis(2026, 9, 29));
    expect(decision && decision.value() && decision.value()->excluded &&
               decision.value()->matchedRuleId == "skip-png",
           "explicit exclusion must stop before the global fallback");
    const auto excludedSignature = nookbit::PlanSignature::rules(rules);
    rules.nodes[0].exclude = false;
    expect(nookbit::PlanSignature::rules(rules) != excludedSignature,
           "changing an exclusion must invalidate a saved plan");
}

} // namespace

// RuleEngine 테스트를 실행하고 결과 코드를 반환하는 함수.
int main() {
    testPriorityAndDestination();
    testConditionsAndNoMatch();
    testUnsafeDestination();
    testOrderedTreeAndOtherwise();
    testAtomicConditionTree();
    if (failures == 0) {
        std::cout << "All rule-engine tests passed.\n";
        return 0;
    }
    std::cerr << failures << " rule-engine assertion(s) failed.\n";
    return 1;
}
