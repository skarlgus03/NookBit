#pragma once

#include <nookbit/domain/result.hpp>
#include <string>

namespace nookbit {

enum class CharacterUpdateKind { Activity, Attention };

/// 특정 기능이나 Qt를 모르는 캐릭터 상태 입력값. 식별자는 발신 기능이 소유한다.
struct CharacterUpdate {
    std::string sourceId;
    std::string entryId;
    CharacterUpdateKind kind{CharacterUpdateKind::Attention};
    bool active{false};
    std::string message;
    std::string cueId;
};

/// 기능별 상태를 전달하는 창구. 같은 sourceId·entryId·kind는 갱신하고 비활성 입력은 제거한다.
/// 호출자는 수신 구현이 요구하는 실행 문맥을 지킨다. 이 계약은 스레드 전환이나 작업 실행을 하지 않는다.
class ICharacterChannel {
public:
    /// 인터페이스를 통해 수신 구현을 안전하게 해제하는 소멸자.
    virtual ~ICharacterChannel() = default;
    /// 식별된 활동·주의 요청을 전달하고 수신 여부를 반환하는 함수.
    [[nodiscard]] virtual Result<void> publish(const CharacterUpdate& update) = 0;
};

} // namespace nookbit
