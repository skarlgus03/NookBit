#include "presentation/initial_setup_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <iostream>

namespace {
int failures{};
// 초기 설정 계약이 달라지면 설명과 함께 검사 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 첫 실행·입력 검증·실패·건너뛰기·재실행 및 비활성 기능을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const auto destination = directory.filePath("organized");
    QSettings preferences(directory.filePath("settings.ini"), QSettings::IniFormat);
    preferences.setValue("destinationRoot", "previous");
    preferences.setValue("character/shown", true);
    preferences.sync();
    nookbit::InitialSetupDefaults defaults{true, true, destination, "current rules", true, true};
    int applies = 0;
    bool reject = false;
    nookbit::InitialSetupRequest received;
    const auto apply = [&](const nookbit::InitialSetupRequest& request) {
        ++applies; received = request;
        return reject ? nookbit::Result<void>::failure({"test.rules", "$.nodes[0]: Invalid destination"}) : nookbit::Result<void>::success();
    };
    nookbit::InitialSetupController setup(preferences, true, defaults, apply);
    expect(setup.required() && setup.existingConfiguration() && setup.destinationRoot() == destination, "existing settings are prefilled without marking setup complete");
    expect(!setup.finish("relative", false, {}, false) && !setup.finish(QDir::rootPath(), false, {}, false)
        && applies == 0, "invalid folders cannot reach feature callbacks");
    QFile file(directory.filePath("not-a-folder")); file.open(QIODevice::WriteOnly); file.close();
    expect(!setup.finish(file.fileName(), false, {}, false), "a file cannot be selected as the destination folder");
    expect(!setup.finish(destination, true, {}, false) && !setup.finish(destination, true, QUrl("https://example.org/rules.json"), false)
        && applies == 0, "import requires a local file selection before applying anything");
    reject = true;
    expect(!setup.finish(destination, true, QUrl::fromLocalFile(directory.filePath("bad.json")), false)
        && setup.required() && setup.message().contains("$.nodes[0]") && !preferences.value("onboarding/completed", false).toBool(),
        "rule validation failure keeps setup open and exposes the error location");
    const auto beforeSkip = applies;
    expect(setup.skip() && applies == beforeSkip && preferences.value("destinationRoot") == "previous"
        && preferences.value("character/shown").toBool(), "skip changes no feature setting and invokes no feature command");
    nookbit::InitialSetupController skipped(preferences, true, defaults, apply);
    expect(!skipped.required() && !skipped.skip() && !skipped.finish(destination, false, {}, true), "skipped setup stays completed on relaunch");
    preferences.remove("onboarding/completed");
    reject = false;
    nookbit::InitialSetupController fresh(preferences, true, defaults, apply);
    expect(fresh.finish(destination, false, QUrl::fromLocalFile("ignored.json"), false)
        && received.destinationRoot == destination && received.rulesFile.isEmpty() && !received.companionShown,
        "keep rules ignores a stale file selection and applies the explicit companion choice");
    expect(!QFileInfo::exists(destination), "setup input handling never creates or moves user files");
    nookbit::InitialSetupController restored(preferences, true, defaults, apply);
    expect(!restored.required(), "successful setup is not shown again");
    auto latest = defaults;
    latest.destinationRoot = directory.filePath("updated-home");
    latest.ruleLabel = "updated rules";
    latest.companionShown = false;
    restored.setDefaultsReader([&] { return latest; });
    const auto beforeReview = applies;
    expect(restored.reopen() && restored.reviewing() && restored.required() && !restored.reopen()
        && restored.destinationRoot() == latest.destinationRoot && restored.ruleLabel() == latest.ruleLabel
        && !restored.companionShown(), "reopen refreshes live settings and rejects overlapping reviews");
    expect(!restored.finish("relative", false, {}, true) && restored.reviewing(), "invalid review remains editable");
    expect(restored.skip() && !restored.reviewing() && applies == beforeReview
        && preferences.value("onboarding/completed").toBool(), "closing a review changes neither feature settings nor completion");
    latest.companionShown = true;
    expect(restored.reopen() && restored.companionShown() && restored.message().isEmpty(), "each review discards stale values and errors");
    expect(restored.finish(latest.destinationRoot, false, {}, true) && applies == beforeReview + 1
        && received.destinationRoot == latest.destinationRoot && !restored.required() && !restored.reviewing(),
        "explicit review apply uses the same feature contract and closes");
    nookbit::InitialSetupController afterReview(preferences, true, defaults, apply);
    expect(!afterReview.required() && !QFileInfo::exists(latest.destinationRoot), "review never re-arms automatic onboarding or creates folders");
    preferences.remove("onboarding/completed");
    nookbit::InitialSetupController disabled(preferences, true, {false, false, {}, {}, false, false}, apply);
    expect(disabled.finish({}, true, QUrl("https://example.org/unused"), true)
        && received.destinationRoot.isEmpty() && received.rulesFile.isEmpty() && !received.companionShown,
        "disabled features receive no synthetic configuration");
    QSettings inspect(directory.filePath("read-only-test.ini"), QSettings::IniFormat);
    nookbit::InitialSetupController verify(inspect, false, defaults, apply);
    expect(!verify.required() && inspect.allKeys().isEmpty(), "normal UI verification neither opens setup nor writes preferences");
    QSettings unwritable(directory.path(), QSettings::IniFormat);
    nookbit::InitialSetupController saveFailure(unwritable, true, defaults, apply);
    expect(!saveFailure.skip() && saveFailure.required() && !saveFailure.message().isEmpty(), "completion-marker save errors cannot report success");
    return failures ? 1 : 0;
}
