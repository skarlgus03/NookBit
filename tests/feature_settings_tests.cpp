#include "app/feature_preferences.hpp"
#include "presentation/feature_settings_controller.hpp"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QVariantMap>
#include <iostream>

namespace {
int failures{};
// 요청 상태·보관 상태·실행 상태가 섞이면 검사 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 화면에 전달한 모듈 행을 안정적인 식별자로 찾는 함수.
QVariantMap row(const nookbit::FeatureSettingsController& controller, const QString& id) {
    for (const auto& value : controller.modules()) if (value.toMap().value("id").toString() == id) return value.toMap();
    return {};
}
// 임시 설정 파일에서 모든 선택 조합과 잘못된 문서를 검사하는 함수.
void testPreferences() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary feature preferences");
    const auto path = directory.filePath("preferences.ini");
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("character/selectedPack", "personal-pack");
    settings.setValue("destinationRoot", "D:/Personal");
    settings.sync();
    nookbit::FeaturePreferences preferences(settings);
    expect(preferences.load() && preferences.load().value() == nookbit::FeatureSelection{}, "first run enables all features");
    expect(!settings.contains("modules/selection"), "loading defaults does not persist a selection");
    for (int mask = 0; mask < 16; ++mask) {
        const nookbit::FeatureSelection selected{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
        expect(bool(preferences.save(selected)), "save selection");
        QSettings reopened(path, QSettings::IniFormat);
        const auto restored = nookbit::FeaturePreferences(reopened).load();
        expect(restored && restored.value() == selected, "restart restores requested selection, including blocked watch");
        expect(reopened.value("character/selectedPack") == "personal-pack" && reopened.value("destinationRoot") == "D:/Personal",
            "feature preferences preserve unrelated data");
    }
    const nookbit::FeatureSelection saved{true, true, false, false};
    expect(bool(preferences.save(saved)), "save CLI baseline");
    const auto before = settings.value("modules/selection");
    const auto overridden = nookbit::parseFeatureSelection({"--disable-feature=file-organizer"}, preferences.load().value());
    expect(overridden && !nookbit::resolveFeatureStartup(overridden.value()).active.watch && !overridden.value().companion,
        "CLI disables overlay saved preferences without enabling other modules");
    expect(settings.value("modules/selection") == before, "CLI overlay never saves temporary choices");
    for (const QByteArray invalid : {QByteArray("not-json"), QByteArray("{}"),
            QByteArray(R"({"schemaVersion":2,"enabled":{}})"),
            QByteArray(R"({"schemaVersion":1,"enabled":{"file-organizer":"false","folder-watch":true,"notifications":true,"companion":true}})")}) {
        settings.setValue("modules/selection", invalid);
        expect(!preferences.load() && settings.value("modules/selection").toByteArray() == invalid,
            "invalid settings fail as a whole without replacing saved bytes");
    }
    QSettings unwritable(directory.path(), QSettings::IniFormat);
    expect(!nookbit::FeaturePreferences(unwritable).save({}), "unwritable preferences report save failure");
}
// 저장 전후 모두 현재 실행이 바뀌지 않으며 재시작 안내만 바뀌는지 검사하는 함수.
void testController() {
    nookbit::FeatureSelection persisted;
    bool failSave = false;
    int writes{};
    nookbit::FeatureSettingsController controller({}, {}, false, [&](nookbit::FeatureSelection selected) {
        ++writes;
        if (failSave) return nookbit::Result<void>::failure({"test.save", "Save failed"});
        persisted = selected;
        return nookbit::Result<void>::success();
    });
    expect(!controller.dirty() && !controller.restartRequired() && !controller.canSave(), "initial state is clean");
    controller.setEnabled("file-organizer", false);
    expect(controller.dirty() && !controller.restartRequired() && controller.canSave(), "draft changes do not become saved restart requests");
    const auto watch = row(controller, "folder-watch");
    expect(watch.value("requested").toBool() && !watch.value("effective").toBool() && watch.value("active").toBool()
        && !watch.value("reason").toString().isEmpty(), "watch request is retained and dependency reason is explicit");
    failSave = true;
    expect(!controller.save() && controller.dirty() && !controller.restartRequired() && persisted.organizer,
        "failed save keeps draft and original saved selection");
    failSave = false;
    expect(controller.save() && !controller.dirty() && controller.restartRequired() && !persisted.organizer && persisted.watch,
        "successful save commits next-run request only");
    expect(row(controller, "file-organizer").value("active").toBool(), "saving never changes current runtime snapshot");
    const auto plan = nookbit::resolveFeatureStartup(persisted);
    nookbit::FeatureSettingsController next(persisted, plan.active, false, [](auto) { return nookbit::Result<void>::success(); });
    expect(!next.restartRequired() && !row(next, "folder-watch").value("active").toBool(), "next run uses dependency-resolved saved state");
    next.setEnabled("file-organizer", true);
    expect(row(next, "folder-watch").value("effective").toBool(), "re-enabling Organizer restores requested Watch without losing its preference");
    next.discard();
    expect(!next.dirty() && !row(next, "file-organizer").value("requested").toBool(), "discard restores saved choice");
    const auto before = controller.modules();
    const int previousWrites = writes;
    controller.setEnabled("typo", false);
    expect(controller.modules() == before && writes == previousWrites, "unknown ID cannot change choices");
    nookbit::FeatureSettingsController recovery({false,false,false,false}, {false,false,false,false}, false,
        [](auto) { return nookbit::Result<void>::success(); }, "Unreadable settings");
    recovery.setEnabled("companion", true);
    recovery.discard();
    expect(!recovery.status().isEmpty() && !recovery.dirty(), "discard keeps invalid-settings repair guidance visible");
    expect(recovery.canSave() && recovery.save() && !recovery.canSave(), "explicit save can repair unreadable preferences");
    nookbit::FeatureSettingsController temporary({}, {false,false,false,false}, true,
        [](auto) { return nookbit::Result<void>::success(); });
    expect(temporary.sessionOverride() && temporary.restartRequired() && !temporary.dirty(), "temporary flags are distinct from saved settings");
}
}
// 모듈 설정 저장과 화면 초안 동작을 사용자 환경 변경 없이 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testPreferences();
    testController();
    return failures ? 1 : 0;
}
