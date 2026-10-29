#include "app/notification_runtime.hpp"
#include "app/folder_watch_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <utility>

namespace {
int failures{};
const nookbit::NotificationType offerType{"file.offer", "folder-watch", "Watch", "Offer", "Test offer", true};
// 기능 묶음의 수명과 설정 결과가 예상과 다르면 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// 알림 묶음 종료 후 컨트롤러는 사라지고 표시 설정은 유지되는지 검사하는 함수.
void testNotificationOwnership() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "notification fixture"); return; }
    QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
    QPointer<nookbit::NotificationController> controller;
    nookbit::UiPageRegistry pages;
    {
        nookbit::NotificationRuntime runtime(preferences);
        controller = &runtime.controller();
        expect(runtime.registerUi(pages).hasValue() && pages.pageId("notifications.settings") == 11
            && pages.page(11).value("properties").toMap().value("controller").value<QObject*>() == controller,
            "notification owner registers its own menu and controller inputs");
        expect(controller->registerType(offerType).hasValue(), "runtime exposes an ordinary notification controller");
        expect(controller->setEnabled("file.offer", false), "runtime stores existing enabled preference");
    }
    expect(controller.isNull() && !preferences.value("notifications/types/file.offer/enabled", true).toBool(),
        "controller dies with owner while preferences survive");
    expect(pages.pages().isEmpty(), "notification UI metadata dies with owner");
    {
        nookbit::NotificationRuntime restored(preferences);
        expect(restored.controller().registerType(offerType).hasValue() && !restored.controller().isEnabled("file.offer"),
            "recreated runtime restores the previous notification preference");
    }
    {
        nookbit::NotificationRuntime transient(preferences, false);
        expect(transient.controller().registerType(offerType).hasValue() && transient.controller().isEnabled("file.offer"),
            "verification mode ignores persisted flags");
        expect(transient.controller().setEnabled("file.offer", false) && transient.controller().setEnabled("file.offer", true),
            "verification flags remain usable in memory");
        expect(!preferences.value("notifications/types/file.offer/enabled", true).toBool(), "verification mode never updates persisted flags");
    }
    nookbit::NotificationRuntime freshTransient(preferences, false);
    expect(freshTransient.controller().registerType(offerType).hasValue() && freshTransient.controller().isEnabled("file.offer"),
        "transient preferences do not outlive their owner");
    QSettings blocked(directory.path(), QSettings::IniFormat);
    nookbit::NotificationRuntime failed(blocked);
    expect(failed.controller().registerType(offerType).hasValue(), "failed-save fixture starts with defaults");
    expect(failed.controller().publish({"file.offer", "offer", true, "Title", "Message"}).hasValue(), "active notification before failed save");
    expect(!failed.controller().setEnabled("file.offer", false) && failed.controller().isEnabled("file.offer") &&
        failed.controller().activeTypes().contains("file.offer") && !blocked.contains("notifications/types/file.offer/enabled"),
        "failed persistence rolls back the value and retains current delivery");
}

class PendingQueue final : public nookbit::IPendingPathsQuery, public nookbit::IEnqueuePendingPaths {
public:
    int enqueues{};
    // 기존 대기 경로가 없는 읽기 전용 결과를 반환하는 함수.
    nookbit::Result<std::vector<std::filesystem::path>> pendingPaths() const override {
        return nookbit::Result<std::vector<std::filesystem::path>>::success({});
    }
    // 생성·종료가 대기 등록을 실행하는지 감지하는 함수.
    nookbit::Result<nookbit::PendingEnqueueResult> enqueuePaths(const std::vector<std::filesystem::path>&) override {
        ++enqueues; return nookbit::Result<nookbit::PendingEnqueueResult>::success({});
    }
};

class Classification final : public nookbit::IClassificationQuery {
public:
    mutable int calls{};
    // 스캔 결과를 받아 파일 내용 읽기나 이동 없이 분류하는 함수.
    nookbit::Result<nookbit::ClassificationSnapshot> classify(const std::vector<nookbit::FileFact>& files, std::int64_t) const override {
        ++calls;
        nookbit::ClassificationSnapshot result; result.rulesRevision = "runtime-test";
        for (const auto& file : files)
            result.files.push_back({file.absolutePath, nookbit::RuleDecision{"photos", L"Pictures", "Matched", std::nullopt}});
        return nookbit::Result<nookbit::ClassificationSnapshot>::success(std::move(result));
    }
};

// 감시 묶음이 기존 설정·대기 시각을 유지하고 종료 시 자동 검사도 취소하는지 검사하는 함수.
void testWatchOwnership() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "watch fixture"); return; }
    const auto watched = directory.filePath("watched");
    expect(QDir().mkpath(watched), "watched directory exists");
    const auto source = watched + "/photo.jpg";
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly) && file.write("photo") == 5, "owned watch file"); file.close();
    const auto schema = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/schemas/watch.schema.json";
    const auto settingsPath = directory.filePath("watch.json");
    nookbit::JsonWatchSettingsRepository repository(settingsPath, schema);
    nookbit::WatchSettings settings;
    settings.folder = std::filesystem::path(watched.toStdWString()); settings.enabled = true;
    settings.conditions = {{nookbit::WatchConditionType::FileCount, 1}};
    expect(repository.save(settings).hasValue(), "saved enabled watch fixture");
    QFile settingsFile(settingsPath);
    expect(settingsFile.open(QIODevice::ReadOnly), "read original settings");
    const auto originalSettings = settingsFile.readAll(); settingsFile.close();
    QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
    PendingQueue pending; Classification classification;
    nookbit::FolderWatchRuntimeConfig config{settingsPath, schema, directory.filePath("fallback"), true};
    QPointer<nookbit::WatchController> controller;
    nookbit::UiPageRegistry pages;
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + 120000;
    {
        nookbit::FolderWatchRuntime runtime(config, classification, pending, pending, preferences);
        controller = &runtime.controller();
        expect(runtime.registerUi(pages).hasValue() && pages.pageId("watch.settings") == 3
            && pages.page(3).value("host").toString() == "settingsDetail" && pages.extensionPages().isEmpty(),
            "watch settings are registered once for the settings slide");
        expect(runtime.settingsEntryUi().value("properties").toMap().value("controller").value<QObject*>() == controller
            && runtime.proposalUi().value("properties").toMap().value("controller").value<QObject*>() == controller,
            "watch owner supplies entry and proposal inputs without exposing them to common settings");
        expect(controller->enabled() && QDir::cleanPath(controller->folder()) == QDir::cleanPath(watched), "saved settings take precedence over fallback folder");
        controller->checkNow();
        expect(classification.calls == 1 && pending.enqueues == 0, "runtime connects real scanner and proposal flow without enqueueing");
        controller->restoreSnoozeUntil(deadline);
        expect(preferences.value("watchSnoozeUntilUtcMs").toLongLong() == deadline &&
            preferences.value("watchSnoozeFolder").toString() == controller->folder(), "snooze uses existing preference keys");
    }
    expect(controller.isNull(), "watch controller dies before its owned dependencies");
    expect(pages.pages().isEmpty(), "watch UI metadata dies with owner");
    {
        nookbit::FolderWatchRuntime restored(config, classification, pending, pending, preferences);
        expect(restored.controller().snoozedUntilUtcMs() == deadline, "same-folder snooze restores after recreation");
    }
    preferences.setValue("watchSnoozeFolder", directory.filePath("another-folder"));
    {
        nookbit::FolderWatchRuntime otherFolder(config, classification, pending, pending, preferences);
        expect(otherFolder.controller().snoozedUntilUtcMs() == 0, "another folder's snooze is not restored");
    }
    preferences.setValue("watchSnoozeFolder", watched);
    config.persistSnooze = false;
    {
        nookbit::FolderWatchRuntime transient(config, classification, pending, pending, preferences);
        expect(transient.controller().snoozedUntilUtcMs() == 0, "verification mode ignores persisted snooze");
        transient.controller().restoreSnoozeUntil(deadline + 10000);
        expect(preferences.value("watchSnoozeUntilUtcMs").toLongLong() == deadline, "verification snooze never updates preferences");
    }
    {
        nookbit::FolderWatchRuntime closing(config, classification, pending, pending, preferences);
        controller = &closing.controller();
        expect(controller->enabled(), "closing owner has active monitoring and a scheduled startup check");
    }
    const auto callsBeforeWait = classification.calls;
    QEventLoop loop; QTimer::singleShot(1150, &loop, &QEventLoop::quit); loop.exec();
    expect(controller.isNull() && classification.calls == callsBeforeWait && pending.enqueues == 0 && QFile::exists(source),
        "destroyed runtime leaves no delayed scan or file work behind");
    expect(settingsFile.open(QIODevice::ReadOnly) && settingsFile.readAll() == originalSettings, "creation and destruction never rewrite watch settings");
}
}

// UI 없이 기능별 객체 소유와 설정 보존을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testNotificationOwnership(); testWatchOwnership();
    return failures ? 1 : 0;
}
