#include "presentation/tutorial_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

namespace {
int failures{};
// 튜토리얼의 완료·재열기 계약이 달라지면 이유와 함께 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 완료 기록 외 개인 설정을 바꾸지 않는지 임시 저장소에서 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings settings(directory.filePath("preferences.ini"), QSettings::IniFormat);
    settings.setValue("destinationRoot", "existing-folder");
    settings.setValue("character/shown", true);
    settings.sync();
    nookbit::TutorialController first(settings, true);
    expect(first.required() && !first.reviewing() && !first.reopen(), "fresh tutorial opens once and cannot overlap itself");
    expect(first.finish() && !first.required() && settings.value("tutorial/completed").toBool(), "finish records only tutorial completion");
    expect(!first.finish() && !first.skip(), "closed tutorial rejects repeated completion");
    nookbit::TutorialController restored(settings, true);
    expect(!restored.required(), "completion suppresses automatic reopening");
    const auto before = settings.allKeys();
    expect(restored.reopen() && restored.reviewing() && restored.required() && !restored.reopen(), "completed tutorial can be explicitly reopened once");
    expect(restored.skip() && !restored.required() && !restored.reviewing() && settings.allKeys() == before,
        "closing review preserves completion and unrelated settings");
    expect(restored.reopen() && restored.finish() && settings.allKeys() == before, "finishing review writes no new preferences");
    expect(settings.value("destinationRoot") == "existing-folder" && settings.value("character/shown").toBool(),
        "reading and completing the tutorial never configures features");
    QSettings skipped(directory.filePath("skip.ini"), QSettings::IniFormat);
    nookbit::TutorialController skip(skipped, true);
    expect(skip.skip() && skipped.value("tutorial/completed").toBool(), "skip suppresses future first-run prompts");
    QSettings legacy(directory.filePath("legacy.ini"), QSettings::IniFormat);
    legacy.setValue("onboarding/completed", true);
    nookbit::TutorialController migrated(legacy, true);
    expect(!migrated.required() && migrated.reopen() && migrated.finish() && !legacy.contains("tutorial/completed"),
        "legacy completion is respected without rewriting it during review");
    QSettings verification(directory.filePath("verify.ini"), QSettings::IniFormat);
    nookbit::TutorialController verify(verification, false);
    expect(!verify.required() && verify.reopen() && verify.finish() && verification.allKeys().isEmpty(),
        "normal UI verification can navigate without writing settings");
    QSettings unwritable(directory.path(), QSettings::IniFormat);
    nookbit::TutorialController failed(unwritable, true);
    expect(!failed.skip() && failed.required() && !failed.message().isEmpty(), "save failure keeps tutorial open with an error");
    return failures ? 1 : 0;
}
