#include "modules/notifications/notification_center.hpp"
#include <algorithm>
#include <utility>

namespace nookbit {
namespace {
// 설정 키와 종류 참조에 사용할 안정적인 식별자인지 검사하는 함수.
bool validId(const std::string& value) {
    return !value.empty() && value.size() <= 160 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}
}
// 알림 종류를 한 번만 등록하고 기본 표시 정책을 보관하는 함수.
Result<void> NotificationCenter::registerType(NotificationType type) {
    if (!validId(type.id) || !validId(type.sourceId) || type.name.empty() || type.sourceName.empty())
        return Result<void>::failure({"notification.invalid_type", "Invalid notification type metadata."});
    if (types_.contains(type.id)) return Result<void>::failure({"notification.duplicate_type", "Notification type is already registered."});
    enabled_[type.id] = type.defaultEnabled;
    const auto id = type.id;
    types_.emplace(id, std::move(type));
    return Result<void>::success();
}
// 표시 정책만 변경하고 기능이나 활성 알림 자체를 제거하지 않는 함수.
Result<void> NotificationCenter::setEnabled(const std::string& typeId, bool value) {
    if (!types_.contains(typeId)) return Result<void>::failure({"notification.unknown_type", "Unknown notification type."});
    enabled_[typeId] = value;
    return Result<void>::success();
}
// 등록되지 않은 종류는 기본적으로 표시하지 않는 함수.
bool NotificationCenter::enabled(const std::string& typeId) const {
    const auto found = enabled_.find(typeId);
    return found != enabled_.end() && found->second;
}
// 종류·인스턴스가 같은 알림은 추가로 쌓지 않고 갱신하는 함수.
Result<void> NotificationCenter::publish(const NotificationUpdate& update) {
    if (!types_.contains(update.typeId)) return Result<void>::failure({"notification.unknown_type", "Unknown notification type."});
    if (update.instanceId.empty() || update.instanceId.size() > 256 || update.instanceId.find('\0') != std::string::npos)
        return Result<void>::failure({"notification.invalid_instance", "A notification instance identifier is required."});
    const auto key = std::make_pair(update.typeId, update.instanceId);
    if (update.active) active_[key] = update;
    else active_.erase(key);
    return Result<void>::success();
}
// 표시를 끈 종류를 제외한 활성 알림 목록을 만드는 함수.
std::vector<NotificationUpdate> NotificationCenter::visible() const {
    std::vector<NotificationUpdate> result;
    for (const auto& [key, update] : active_)
        if (enabled(key.first)) result.push_back(update);
    return result;
}
}
