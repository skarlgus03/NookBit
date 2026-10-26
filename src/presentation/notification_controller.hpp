#pragma once
#include "modules/notifications/notification_center.hpp"
#include <QObject>
#include <QVariantList>
#include <QStringList>
#include <functional>

namespace nookbit {
/// 공통 알림을 화면 값과 변경 신호로 연결하며 표시 수단과 업무 기능을 모르는 어댑터.
class NotificationController final : public QObject, public INotificationChannel {
    Q_OBJECT
    Q_PROPERTY(QVariantList types READ types NOTIFY typesChanged)
    Q_PROPERTY(QStringList activeTypes READ activeTypes NOTIFY activeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    using ReadEnabled = std::function<bool(const QString&, bool)>;
    using SaveEnabled = std::function<Result<void>(const QString&, bool)>;
    /// 사용자 설정 읽기·쓰기 창구를 주입하는 생성자.
    NotificationController(ReadEnabled read, SaveEnabled save, QObject* parent = nullptr);
    /// 기능이 제공하는 알림 종류를 등록하고 표시 설정을 복원하는 함수.
    [[nodiscard]] Result<void> registerType(NotificationType type);
    /// 등록 정보와 표시 설정을 화면 목록으로 변환하는 함수.
    QVariantList types() const;
    /// 현재 표시되는 알림 종류 목록을 반환하는 함수.
    QStringList activeTypes() const { return activeTypes_; }
    /// 설정 저장 오류를 반환하는 함수.
    QString status() const { return status_; }
    /// 저장에 성공했을 때만 표시 정책을 바꾸는 함수.
    Q_INVOKABLE bool setEnabled(const QString& typeId, bool value);
    /// 등록된 종류의 현재 표시 정책을 반환하는 함수.
    Q_INVOKABLE bool isEnabled(const QString& typeId) const;
    /// 발생 상태를 갱신하고 표시할 내용이 바뀐 경우에만 전달하는 함수.
    [[nodiscard]] Result<void> publish(const NotificationUpdate& update) override;
signals:
    void typesChanged();
    void activeChanged();
    void statusChanged();
    void notificationChanged(const QString& typeId, const QString& sourceId, const QString& instanceId,
        bool active, const QString& title, const QString& message);
private:
    /// 표시 목록의 추가·갱신·해제 차이만 알리는 함수.
    void syncDelivery();
    NotificationCenter center_;
    ReadEnabled read_;
    SaveEnabled save_;
    QString status_;
    QStringList activeTypes_;
    std::map<std::pair<std::string, std::string>, NotificationUpdate> delivered_;
};
}
