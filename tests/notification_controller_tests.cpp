#include "presentation/notification_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
// 예상과 다른 화면 전달 또는 저장 결과를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 알림 창구의 전달 중복 방지·표시 전환·저장 실패·재실행 복원을 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir folder;
    if (!folder.isValid()) return 1;
    const auto path = folder.filePath("notifications.ini");
    QSettings preferences(path, QSettings::IniFormat);
    bool failSave = false;
    const nookbit::NotificationType type{"file.offer", "folder-watch", "Folder watch", "Offer", "Organize files", true};
    nookbit::NotificationController controller(
        [&preferences](const QString& id, bool fallback) {
            // 새 인스턴스에서도 사용할 표시 설정을 읽는 함수.
            return preferences.value("notifications/types/" + id + "/enabled", fallback).toBool();
        },
        [&preferences, &failSave](const QString& id, bool value) {
            // 저장 실패를 주입할 수 있는 사용자 설정 보관 함수.
            if (failSave) return nookbit::Result<void>::failure({"test.save_failed", "Save failed"});
            preferences.setValue("notifications/types/" + id + "/enabled", value);
            preferences.sync();
            return nookbit::Result<void>::success();
        });
    expect(controller.registerType(type).hasValue(), "type registration restores defaults");
    std::vector<bool> changes;
    QString lastMessage;
    QObject::connect(&controller, &nookbit::NotificationController::notificationChanged,
        [&](const QString&, const QString& source, const QString&, bool active, const QString&, const QString& message) {
            // 표시 수단이 전달받는 활성·해제 정보만 기록하는 함수.
            expect(source == "folder-watch", "source identity is preserved");
            changes.push_back(active);
            lastMessage = message;
        });
    nookbit::INotificationChannel& channel = controller;
    nookbit::NotificationUpdate notice{"file.offer", "offer", true, "Offer", "original"};
    expect(channel.publish(notice).hasValue() && channel.publish(notice).hasValue() && changes.size() == 1,
        "repeated identical updates must not replay reactions");
    expect(controller.setEnabled("file.offer", false) && !changes.back() && controller.activeTypes().isEmpty(),
        "muting withdraws delivery without resolving the source");
    notice.message = "latest";
    expect(channel.publish(notice).hasValue() && changes.size() == 2, "muted updates stay silent");
    expect(controller.setEnabled("file.offer", true) && changes.back() && lastMessage == "latest", "reenabling uses latest active content");
    failSave = true;
    const auto beforeFailure = changes.size();
    expect(!controller.setEnabled("file.offer", false) && controller.isEnabled("file.offer") &&
        changes.size() == beforeFailure && !controller.status().isEmpty(), "save failure preserves both policy and delivery");
    failSave = false;
    expect(controller.setEnabled("file.offer", false), "muting can succeed after a save error");
    notice.active = false;
    expect(channel.publish(notice).hasValue() && controller.setEnabled("file.offer", true) && controller.activeTypes().isEmpty(),
        "resolved muted notices must not reappear");
    expect(controller.registerType({"extra.notice", "extra", "Extra", "Another type", "New feature", true}).hasValue() && controller.types().size() == 2,
        "additional sources appear without changing UI logic");
    expect(controller.setEnabled("file.offer", false), "disabled setting is saved for restart");
    QSettings reopened(path, QSettings::IniFormat);
    nookbit::NotificationController restored(
        [&reopened](const QString& id, bool fallback) {
            // 다시 연 설정 파일에서 정책을 복원하는 함수.
            return reopened.value("notifications/types/" + id + "/enabled", fallback).toBool();
        }, [](const QString&, bool) { return nookbit::Result<void>::success(); });
    expect(restored.registerType(type).hasValue() && !restored.isEnabled("file.offer"), "new controller restores disabled policy");
    notice.active = true;
    expect(restored.publish(notice).hasValue() && restored.activeTypes().isEmpty(), "restored policy filters first incoming notice");
    return failures ? 1 : 0;
}
