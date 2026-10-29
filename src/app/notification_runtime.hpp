#pragma once
#include "presentation/notification_controller.hpp"
#include <QMap>
#include <QSettings>

namespace nookbit {
class UiPageRegistry;
/// 알림 컨트롤러와 표시 설정 연결을 한 수명으로 소유하는 조립 묶음.
/// 외부 설정 객체는 이 묶음보다 오래 살아 있어야 한다.
class NotificationRuntime final {
public:
    /// 기존 표시 설정과 선택적인 메모리 전용 검증 모드를 연결하는 생성자.
    explicit NotificationRuntime(QSettings& preferences, bool persistEnabled = true);
    /// 컨트롤러를 설정 연결보다 먼저 해제하는 소멸자.
    ~NotificationRuntime() = default;
    /// 알림 설정 화면과 메뉴·입력을 기능 수명에 묶어 등록하는 함수.
    Result<void> registerUi(UiPageRegistry& registry);
    /// 이 묶음이 살아 있는 동안만 유효한 알림 화면·입력 창구를 반환하는 함수.
    NotificationController& controller() { return controller_; }
private:
    /// 알림 종류별 표시 설정을 영속 또는 메모리 저장소에서 읽는 함수.
    bool readEnabled(const QString& id, bool fallback) const;
    /// 저장 실패 시 이전 값을 보존하고 표시 설정만 기록하는 함수.
    Result<void> saveEnabled(const QString& id, bool value);

    QSettings& preferences_;
    bool persistEnabled_;
    QMap<QString, bool> transientEnabled_;
    NotificationController controller_;
    /// 등록 화면을 컨트롤러보다 먼저 정리하는 수명 문맥.
    QObject uiLifetime_;
};
}
