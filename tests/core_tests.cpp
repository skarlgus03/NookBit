#include <nookbit/domain/model.hpp>
#include "modules/planner/collision_resolver.hpp"
#include "modules/planner/path_safety_validator.hpp"

#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testSafetyInvariants() {
    expect(nookbit::SafetyInvariants::approvalRequired,
           "approval must always be required");
    expect(!nookbit::SafetyInvariants::permanentDeleteAllowed,
           "permanent deletion must never be allowed");
    expect(!nookbit::SafetyInvariants::silentOverwriteAllowed,
           "silent overwrite must never be allowed");
    expect(!nookbit::SafetyInvariants::followReparsePoints,
           "reparse points must not be followed in the first release");
}

void testCollisionRename() {
    const auto requested = std::filesystem::path(L"C:/home/Documents/report.pdf");
    const std::set<std::filesystem::path> occupied{
        requested,
        std::filesystem::path(L"C:/home/Documents/report (1).pdf"),
        std::filesystem::path(L"C:/home/Documents/report (2).pdf"),
    };

    const auto result = nookbit::CollisionResolver::resolve(
        requested,
        nookbit::CollisionPolicy::Rename,
        [&occupied](const auto& path) { return occupied.contains(path); });

    expect(result.hasValue(), "rename should find an available destination");
    if (result) {
        expect(result.value() == std::filesystem::path(L"C:/home/Documents/report (3).pdf"),
               "rename should preserve the extension and increment the suffix");
    }
}

void testCollisionSkipAndAbort() {
    const auto requested = std::filesystem::path(L"C:/home/report.pdf");
    const auto alwaysExists = [](const auto&) { return true; };

    const auto skip = nookbit::CollisionResolver::resolve(
        requested, nookbit::CollisionPolicy::Skip, alwaysExists);
    expect(!skip.hasValue() && skip.error().code == "collision.skip",
           "skip must return an explicit non-success result");

    const auto abort = nookbit::CollisionResolver::resolve(
        requested, nookbit::CollisionPolicy::Abort, alwaysExists);
    expect(!abort.hasValue() && abort.error().code == "collision.abort",
           "abort must return a distinguishable error code");
}

void testPathContainment() {
    const auto sandbox = std::filesystem::temp_directory_path() / "pco-core-tests";
    const auto watch = sandbox / "watch";
    const auto home = sandbox / "home";
    const auto appData = sandbox / "app-data";

    std::filesystem::create_directories(watch);
    std::filesystem::create_directories(home / "Documents");
    std::filesystem::create_directories(appData);

    const auto safe = nookbit::PathSafetyValidator::validateMove(
        watch / "report.pdf",
        home / "Documents" / "report.pdf",
        {watch / "report.pdf"},
        home,
        appData);
    expect(safe.hasValue(), "an explicitly selected file may move into the home root");

    const auto sourceEscape = nookbit::PathSafetyValidator::validateMove(
        sandbox / "outside.pdf",
        home / "Documents" / "outside.pdf",
        {watch / "report.pdf"},
        home,
        appData);
    expect(!sourceEscape.hasValue() &&
               sourceEscape.error().code == "path.source_not_selected",
           "a file outside the selection must be rejected");

    const auto sibling = nookbit::PathSafetyValidator::validateMove(
        watch / "other.pdf",
        home / "Documents" / "other.pdf",
        {watch / "report.pdf"},
        home,
        appData);
    expect(!sibling.hasValue() && sibling.error().code == "path.source_not_selected",
           "selecting one file must not authorize its whole folder");

    const auto destinationEscape = nookbit::PathSafetyValidator::validateMove(
        watch / "report.pdf",
        home / ".." / "escaped.pdf",
        {watch / "report.pdf"},
        home,
        appData);
    expect(!destinationEscape.hasValue() &&
               destinationEscape.error().code == "path.destination_outside_home",
           "a destination that escapes the home root must be rejected");

    const auto appDataTarget = nookbit::PathSafetyValidator::validateMove(
        watch / "report.pdf",
        appData / "report.pdf",
        {watch / "report.pdf"},
        sandbox,
        appData);
    expect(!appDataTarget.hasValue() &&
               appDataTarget.error().code == "path.app_data_protected",
           "application data must be protected from organization rules");

    std::error_code ignored;
    std::filesystem::remove_all(sandbox, ignored);
}

// 외부 목적지의 명시 허용·범위 제한·앱 데이터 보호를 검사하는 함수.
void testExternalDestinationPermission() {
    const auto sandbox = std::filesystem::temp_directory_path() / "nookbit-external-permission";
    const auto home = sandbox / "home";
    const auto external = sandbox / "external";
    const auto source = sandbox / "input.txt";
    const auto appData = sandbox / "app-data";
    const auto denied = nookbit::PathSafetyValidator::validateMove(source, external / "input.txt", {source}, home, appData);
    expect(!denied, "external destination denied by default");
    expect(nookbit::PathSafetyValidator::validateMove(source, external / "child/input.txt", {source}, home, appData, {external}).hasValue(),
           "explicit external folder permits its children");
    expect(!nookbit::PathSafetyValidator::validateMove(source, sandbox / "external-sibling/input.txt", {source}, home, appData, {external}),
           "permission must not cover similarly named siblings");
    expect(!nookbit::PathSafetyValidator::validateMove(source, external / "../escaped.txt", {source}, home, appData, {external}),
           "permission must not cover traversal escape");
    expect(!nookbit::PathSafetyValidator::validateMove(source, appData / "input.txt", {source}, home, appData, {appData}),
           "allow list must not bypass app data protection");
    expect(!nookbit::PathSafetyValidator::validateMove(source, external / "input.txt", {source}, home, appData, {sandbox.root_path()}),
           "drive root cannot authorize every destination");
}

} // namespace

int main() {
    testSafetyInvariants();
    testCollisionRename();
    testCollisionSkipAndAbort();
    testPathContainment();
    testExternalDestinationPermission();

    if (failures == 0) {
        std::cout << "All core safety tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
