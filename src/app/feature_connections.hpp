#pragma once
#include <nookbit/application/character_channel.hpp>
#include <nookbit/application/notification_channel.hpp>
#include <QObject>

namespace nookbit {
class WatchController;
class NotificationController;
class BackgroundTaskRunner;

/// 폴더 감시 기능이 제공하는 기존 정리 제안 종류를 반환하는 함수.
NotificationType folderWatchOfferType();

/// 기능 사이의 신호와 입력 계약을 연결하며 업무 객체나 표시 수단은 소유하지 않는다.
/// 전달 대상 문맥은 채널의 수명을 대표하며 연결부와 같은 스레드에 있어야 한다.
/// 채널은 전달 대상 문맥이 살아 있는 동안 유효해야 한다.
class FeatureConnections final : public QObject {
public:
    /// 연결만 소유하는 조립부 문맥을 준비하는 생성자.
    explicit FeatureConnections(QObject* parent = nullptr);
    /// 연결부가 사라지면 Qt가 모든 연결과 대기 중 전달을 해제하는 소멸자.
    ~FeatureConnections() override = default;

    /// 감시 제안의 현재 상태와 변경을 공통 알림 창구에 전달하는 함수.
    [[nodiscard]] Result<void> watchToNotifications(WatchController& watch,
        QObject& recipientContext, INotificationChannel& channel);
    /// 표시가 허용된 알림 변경을 컴패니언 주의 입력으로 변환하는 함수.
    [[nodiscard]] Result<void> notificationsToCompanion(NotificationController& notifications,
        QObject& recipientContext, ICharacterChannel& channel);
    /// 실제 파일 작업 상태를 알림 표시 정책과 무관하게 컴패니언에 전달하는 함수.
    [[nodiscard]] Result<void> workToCompanion(BackgroundTaskRunner& tasks,
        QObject& recipientContext, ICharacterChannel& channel);
    /// 파일 작업의 성공·실패 결과만 일시 표현 창구에 전달하는 함수.
    [[nodiscard]] Result<void> workResultsToCompanion(BackgroundTaskRunner& tasks,
        QObject& recipientContext, ICharacterReactionChannel& channel);
};
}
