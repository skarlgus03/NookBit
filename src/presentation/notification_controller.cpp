#include "presentation/notification_controller.hpp"
#include <QThread>
#include <utility>

namespace nookbit {
// 저장 구현을 직접 소유하지 않고 전달받은 창구만 보관하는 생성자.
NotificationController::NotificationController(ReadEnabled read, SaveEnabled save, QObject* parent)
    : QObject(parent), read_(std::move(read)), save_(std::move(save)) {}

// 등록 검증 후 저장된 표시 정책을 복원하는 함수.
Result<void> NotificationController::registerType(NotificationType type) {
    if (QThread::currentThread() != thread()) return Result<void>::failure({"notification.wrong_thread", "Register on the owning thread."});
    const auto id = type.id;
    const bool fallback = type.defaultEnabled;
    auto result = center_.registerType(std::move(type));
    if (!result) return result;
    result = center_.setEnabled(id, read_(QString::fromStdString(id), fallback));
    if (!result) return result;
    emit typesChanged();
    return Result<void>::success();
}

// 알림 화면이 기능 종류를 하드코딩하지 않도록 등록 정보를 값으로 전달하는 함수.
QVariantList NotificationController::types() const {
    QVariantList result;
    for (const auto& [id, type] : center_.types()) result.append(QVariantMap{
        {"id", QString::fromStdString(id)}, {"sourceId", QString::fromStdString(type.sourceId)},
        {"sourceName", QString::fromStdString(type.sourceName)}, {"name", QString::fromStdString(type.name)},
        {"description", QString::fromStdString(type.description)}, {"enabled", center_.enabled(id)}});
    return result;
}

// 표시 정책만 저장하며 저장 실패 시 기존 표시 상태를 보존하는 함수.
bool NotificationController::setEnabled(const QString& id, bool value) {
    if (QThread::currentThread() != thread()) return false;
    if (!center_.types().contains(id.toStdString())) return false;
    if (center_.enabled(id.toStdString()) == value) return true;
    const auto saved = save_(id, value);
    if (!saved) {
        status_ = QString::fromStdString(saved.error().message);
        emit statusChanged();
        emit typesChanged();
        return false;
    }
    const auto changed = center_.setEnabled(id.toStdString(), value);
    if (!changed) return false;
    status_.clear();
    syncDelivery();
    emit typesChanged();
    emit statusChanged();
    return true;
}

// 등록 정보와 분리된 사용자 표시 정책을 조회하는 함수.
bool NotificationController::isEnabled(const QString& id) const { return center_.enabled(id.toStdString()); }

// GUI 실행 문맥에서 알림을 받아 중복 없이 표시 변경만 전달하는 함수.
Result<void> NotificationController::publish(const NotificationUpdate& update) {
    if (QThread::currentThread() != thread()) return Result<void>::failure({"notification.wrong_thread", "Publish on the owning thread."});
    const auto result = center_.publish(update);
    if (result) syncDelivery();
    return result;
}

// 표시 허용 여부와 발생 상태를 합쳐 전달 대상의 차이를 계산하는 함수.
void NotificationController::syncDelivery() {
    decltype(delivered_) next;
    QStringList activeTypes;
    for (const auto& update : center_.visible()) {
        next.emplace(std::make_pair(update.typeId, update.instanceId), update);
        const auto id = QString::fromStdString(update.typeId);
        if (!activeTypes.contains(id)) activeTypes.append(id);
    }
    const auto previous = std::exchange(delivered_, std::move(next));
    if (activeTypes_ != activeTypes) { activeTypes_ = activeTypes; emit activeChanged(); }
    // Qt 출력 이벤트로 바꾸되 컴패니언 상태나 업무 호출을 포함하지 않는 함수.
    const auto send = [this](const NotificationUpdate& value, bool active) {
        const auto& type = center_.types().at(value.typeId);
        emit notificationChanged(QString::fromStdString(value.typeId), QString::fromStdString(type.sourceId),
            QString::fromStdString(value.instanceId), active, QString::fromStdString(value.title), QString::fromStdString(value.message));
    };
    for (const auto& [key, value] : previous) if (!delivered_.contains(key)) send(value, false);
    for (const auto& [key, value] : delivered_) {
        const auto old = previous.find(key);
        if (old == previous.end() || old->second != value) send(value, true);
    }
}
}
