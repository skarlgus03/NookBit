#include "modules/rules/rule_engine.hpp"

#include <chrono>
#include <iostream>
#include <string>

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

} // namespace

// RuleEngine 테스트를 실행하고 결과 코드를 반환하는 함수.
int main() {
    testPriorityAndDestination();
    testConditionsAndNoMatch();
    testUnsafeDestination();
    if (failures == 0) {
        std::cout << "All rule-engine tests passed.\n";
        return 0;
    }
    std::cerr << failures << " rule-engine assertion(s) failed.\n";
    return 1;
}
