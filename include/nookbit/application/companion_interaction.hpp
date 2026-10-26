#pragma once
#include <nookbit/domain/result.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace nookbit {
enum class InteractionMode { Once, Continuous };
enum class InteractionPhase { Invoke, Begin, Update, End, Cancel };
enum class InteractionStatus { Accepted, Completed, Cancelled, Rejected };

struct InteractionType {
    std::string id;
    std::string sourceId;
    std::string name;
    InteractionMode mode{InteractionMode::Once};
};
struct InteractionContext {
    std::string behaviorId;
    std::string reactionId;
};
struct InteractionPoint { double x{0}; double y{0}; };
using InteractionValue = std::variant<std::string, double, bool>;

/// 입력 담당자가 발생 사실만 전달하는 값. 함수나 작업자 포인터를 담지 않는다.
struct InteractionRequest {
    std::uint64_t instanceId{0};
    std::string typeId;
    std::string targetId;
    InteractionPhase phase{InteractionPhase::Invoke};
    InteractionContext context;
    std::optional<InteractionPoint> point;
    std::map<std::string, InteractionValue> values;
};
/// 효과 처리 결과와 별도로 표현 담당자에게 요청할 일시 반응.
struct InteractionReaction {
    std::string cueId;
    std::string message;
    int durationMs{0}; // 0이면 연속 입력이 끝날 때까지 유지한다.
};
struct InteractionOutcome {
    std::uint64_t instanceId{0};
    InteractionStatus status{InteractionStatus::Rejected};
    std::string reason;
    std::optional<InteractionReaction> reaction;
};
/// Qt 입력과 개별 기능을 모르는 상호작용 요청 창구.
class IInteractionChannel {
public:
    /// 인터페이스를 통해 안전하게 해제하는 소멸자.
    virtual ~IInteractionChannel() = default;
    /// 상호작용 요청을 처리하고 수락·종료·거절 결과를 반환하는 함수.
    [[nodiscard]] virtual InteractionOutcome submit(const InteractionRequest& request) = 0;
};
}
