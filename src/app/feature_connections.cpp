#include "app/feature_connections.hpp"
#include "presentation/watch_controller.hpp"
#include "presentation/notification_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <QPointer>
#include <QTextStream>
#include <QThread>

namespace nookbit {
namespace {
constexpr auto offerType = "file.offer";
constexpr auto offerInstance = "organization-offer";

// 연결부와 양쪽 객체가 같은 소유 스레드에서 사용되는지 확인하는 함수.
Result<void> validateContext(const QObject& bridge, const QObject& source, const QObject& recipient) {
    if (QThread::currentThread() != bridge.thread() || source.thread() != bridge.thread() || recipient.thread() != bridge.thread())
        return Result<void>::failure({"connections.wrong_thread", "Feature connections require a shared owning thread."});
    return Result<void>::success();
}

// 표시 전달 실패만 기록하고 원래 기능의 작업 결과는 변경하지 않는 함수.
void reportDelivery(const Result<void>& result) {
    if (!result) QTextStream(stderr) << QString::fromStdString(result.error().message) << Qt::endl;
}
}

// 기존 설정 키와 상황 이름을 바꾸지 않고 알림 등록 정보를 만드는 함수.
NotificationType folderWatchOfferType() {
    return {offerType, "folder-watch", "폴더 감시", "정리 제안", "감시 폴더에서 정리할 파일이 발견되면 알려줘요.", true};
}

// 객체를 생성하지 않고 연결의 해제 문맥만 준비하는 생성자.
FeatureConnections::FeatureConnections(QObject* parent) : QObject(parent) {}

// 감시 기능을 알림 표시 구현과 분리하고 초기 제안 상태도 전달하는 함수.
Result<void> FeatureConnections::watchToNotifications(WatchController& watch,
    QObject& recipientContext, INotificationChannel& channel) {
    const auto valid = validateContext(*this, watch, recipientContext);
    if (!valid) return valid;
    const QPointer<WatchController> source(&watch);
    const QPointer<QObject> recipient(&recipientContext);
    // 제안 상태만 복사해 전달하고 파괴된 발신자나 수신자에는 접근하지 않는 함수.
    const auto report = [source, recipient, &channel] {
        if (!source || !recipient) return;
        reportDelivery(channel.publish({offerType, offerInstance, source->proposalVisible(), "정리 제안", "정리할 파일이 있어요!"}));
    };
    QObject::connect(&watch, &WatchController::watchChanged, this, report);
    report();
    return Result<void>::success();
}

// 공통 알림 변경을 특정 컴패니언 구현을 모르는 입력 계약으로 전달하는 함수.
Result<void> FeatureConnections::notificationsToCompanion(NotificationController& notifications,
    QObject& recipientContext, ICharacterChannel& channel) {
    const auto valid = validateContext(*this, notifications, recipientContext);
    if (!valid) return valid;
    const QPointer<NotificationController> source(&notifications);
    const QPointer<QObject> recipient(&recipientContext);
    QObject::connect(&notifications, &NotificationController::notificationChanged, this,
        [source, recipient, &channel](const QString& typeId, const QString& sourceId, const QString& instanceId,
            bool active, const QString&, const QString& message) {
            // 원래 발신자·종류·인스턴스 식별자를 유지해 주의 상태를 갱신하는 함수.
            if (!source || !recipient) return;
            reportDelivery(channel.publish({sourceId.toStdString(), (typeId + "/" + instanceId).toStdString(),
                CharacterUpdateKind::Attention, active, message.toStdString(), typeId.toStdString()}));
        });
    return Result<void>::success();
}

// 검사 작업과 실제 파일 작업을 구분해 독립적인 활동 입력으로 전달하는 함수.
Result<void> FeatureConnections::workToCompanion(BackgroundTaskRunner& tasks,
    QObject& recipientContext, ICharacterChannel& channel) {
    const auto valid = validateContext(*this, tasks, recipientContext);
    if (!valid) return valid;
    const QPointer<BackgroundTaskRunner> source(&tasks);
    const QPointer<QObject> recipient(&recipientContext);
    // 알림 설정을 읽지 않고 현재 작업 활동만 보고하는 함수.
    const auto report = [source, recipient, &channel] {
        if (!source || !recipient) return;
        reportDelivery(channel.publish({"file-operations", "current-operation", CharacterUpdateKind::Activity,
            source->fileWorkActive(), {}, "file.work"}));
    };
    QObject::connect(&tasks, &BackgroundTaskRunner::busyChanged, this, report);
    report();
    return Result<void>::success();
}
}
