#pragma once
#include <nookbit/domain/result.hpp>
#include <string>

namespace nookbit {
/// 기능이 제공하는 알림 종류. 기능 활성화나 표시 수단을 포함하지 않는다.
struct NotificationType {
    std::string id;
    std::string sourceId;
    std::string sourceName;
    std::string name;
    std::string description;
    bool defaultEnabled{true};
};

/// 같은 종류·인스턴스의 발생/갱신/해제를 전달하는 알림 데이터.
struct NotificationUpdate {
    std::string typeId;
    std::string instanceId;
    bool active{false};
    std::string title;
    std::string message;
    bool operator==(const NotificationUpdate&) const = default;
};

/// 표시 담당자를 모르는 공통 알림 창구. 스레드 전환과 실제 작업은 수행하지 않는다.
class INotificationChannel {
public:
    /// 인터페이스를 통해 안전하게 해제하는 소멸자.
    virtual ~INotificationChannel() = default;
    /// 발생 상태를 전달하며 수신 구현이 정한 실행 문맥에서 호출하는 함수.
    [[nodiscard]] virtual Result<void> publish(const NotificationUpdate& update) = 0;
};
}
