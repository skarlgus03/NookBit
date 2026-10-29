#pragma once
#include <nookbit/application/companion_interaction.hpp>
#include <functional>

namespace nookbit {
/// 종류 등록·입력 검증·세션만 관리하며 효과 구현은 주입받는 관리자.
class InteractionManager final : public IInteractionChannel, public IInteractionRegistry {
public:
    using Handler = IInteractionRegistry::Handler;
    using CanInvoke = IInteractionRegistry::CanInvoke;
    /// 새 종류와 효과 처리 창구를 등록하는 함수.
    [[nodiscard]] Result<void> registerType(InteractionType type, Handler handler, CanInvoke canInvoke = {}) override;
    /// 처리기 소유 모듈이 끝날 때 해당 등록과 세션을 제거하는 함수.
    void unregisterType(const std::string& id) override;
    /// 등록된 단발 메뉴와 대상·현재 상황에 따른 실행 가능 여부를 반환하는 함수.
    std::vector<InteractionMenuAction> menuActions(const std::string& target, const InteractionContext& context) const;
    /// 대상 표시 가능 여부를 바꾸고 사라진 대상의 세션을 취소하는 함수.
    void setTargetAvailable(const std::string& id, bool available);
    /// 종류의 사용 여부를 바꾸며 진행 중인 해당 세션도 취소하는 함수.
    [[nodiscard]] Result<void> setEnabled(const std::string& typeId, bool enabled);
    /// 단발 입력 또는 연속 세션의 단계를 처리하는 함수.
    [[nodiscard]] InteractionOutcome submit(const InteractionRequest& request) override;
    /// 대상의 현재 세션을 취소하는 함수.
    void cancel(const std::string& targetId);
private:
    struct Entry { InteractionType type; Handler handler; CanInvoke canInvoke; bool enabled{true}; };
    struct Target {
        bool available{false};
        std::uint64_t lastStarted{0};
        std::optional<InteractionRequest> session;
    };
    std::map<std::string, Entry> types_;
    std::map<std::string, Target> targets_;
};

/// 기본 클릭·끌기·쓰다듬기 처리기를 공통 관리자에 등록하는 함수.
[[nodiscard]] Result<void> registerBasicCompanionInteractions(InteractionManager& manager);
}
