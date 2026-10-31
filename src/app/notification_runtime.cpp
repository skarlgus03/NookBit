#include "app/notification_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include <QCoreApplication>

namespace nookbit {
// 알림 표시 정책만 변경하는 화면을 등록하고 실제 기능 명령과 분리하는 함수.
Result<void> NotificationRuntime::registerUi(UiPageRegistry& registry) {
    const auto text = [](const char* value) {
        // 알림 모듈이 소유한 안내 문구를 번역하는 함수.
        return QCoreApplication::translate("NotificationTutorial", value);
    };
    const auto tutorial = registry.addTutorialSteps({
        {"notifications.basics", "notifications", text("알림 골라 받기"),
            text("모듈이 등록한 알림 종류를 한곳에서 관리해요."),
            {text("메인 메뉴에서 알림을 열어요."),
             text("받고 싶은 알림만 켜 두세요.")},
            text("알림을 끄는 것과 해당 기능 모듈을 끄는 것은 달라요. 기능 자체를 끄려면 모듈 관리를 사용해 주세요."), 60}
    }, uiLifetime_);
    if (!tutorial) return tutorial;
    return registry.addPage({"notifications.settings", "notifications", 11,
        QCoreApplication::translate("NotificationUi", "알림"), "notifications", {}, 60,
        QUrl(QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/NotificationsPage.qml")),
        {{"controller", QVariant::fromValue(static_cast<QObject*>(&controller_))}}}, uiLifetime_);
}
// 콜백이 참조하는 설정 상태를 먼저 만들고 알림 컨트롤러를 생성하는 함수.
NotificationRuntime::NotificationRuntime(QSettings& preferences, bool persistEnabled)
    : preferences_(preferences), persistEnabled_(persistEnabled),
      controller_([this](const QString& id, bool fallback) {
          // 알림 등록 시 현재 표시 정책을 조회하는 함수.
          return readEnabled(id, fallback);
      }, [this](const QString& id, bool value) {
          // 컨트롤러의 설정 변경 요청을 소유한 저장 연결에 전달하는 함수.
          return saveEnabled(id, value);
      }) {}

// 검증 모드에서는 실제 사용자 설정을 읽지 않는 함수.
bool NotificationRuntime::readEnabled(const QString& id, bool fallback) const {
    return persistEnabled_ ? preferences_.value(QStringLiteral("notifications/types/") + id + "/enabled", fallback).toBool()
                           : transientEnabled_.value(id, fallback);
}

// 기존 설정 키와 저장 실패 복원 동작을 유지하는 함수.
Result<void> NotificationRuntime::saveEnabled(const QString& id, bool value) {
    if (!persistEnabled_) { transientEnabled_.insert(id, value); return Result<void>::success(); }
    const auto key = QStringLiteral("notifications/types/") + id + "/enabled";
    const bool existed = preferences_.contains(key);
    const auto previous = preferences_.value(key);
    preferences_.setValue(key, value);
    preferences_.sync();
    if (preferences_.status() == QSettings::NoError) return Result<void>::success();
    if (existed) preferences_.setValue(key, previous); else preferences_.remove(key);
    preferences_.sync();
    return Result<void>::failure({"notification.save_failed", "알림 설정을 저장하지 못했어요. 이전 설정을 유지해요."});
}
}
