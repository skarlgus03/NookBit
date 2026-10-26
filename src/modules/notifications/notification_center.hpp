#pragma once
#include <nookbit/application/notification_channel.hpp>
#include <map>
#include <vector>

namespace nookbit {
/// 등록·활성 상태·표시 정책을 관리하며 Qt, 컴패니언과 작업 모듈을 모르는 알림 중심부.
class NotificationCenter final : public INotificationChannel {
public:
    /// 중복되지 않는 알림 종류를 등록하는 함수.
    [[nodiscard]] Result<void> registerType(NotificationType type);
    /// 등록된 종류의 표시 허용 여부만 변경하는 함수.
    [[nodiscard]] Result<void> setEnabled(const std::string& typeId, bool enabled);
    /// 등록된 종류가 표시를 허용하는지 반환하는 함수.
    [[nodiscard]] bool enabled(const std::string& typeId) const;
    /// 등록된 알림 종류를 반환하는 함수.
    [[nodiscard]] const std::map<std::string, NotificationType>& types() const { return types_; }
    /// 발생 상태를 갱신하거나 해제하며 표시 금지 상태에서도 사실을 유지하는 함수.
    [[nodiscard]] Result<void> publish(const NotificationUpdate& update) override;
    /// 현재 표시가 허용된 활성 알림만 반환하는 함수.
    [[nodiscard]] std::vector<NotificationUpdate> visible() const;
private:
    std::map<std::string, NotificationType> types_;
    std::map<std::string, bool> enabled_;
    std::map<std::pair<std::string, std::string>, NotificationUpdate> active_;
};
}
