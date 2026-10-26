#include "modules/character/interaction_manager.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace nookbit {
namespace {
// 종류·대상 식별자로 쓸 수 있는 값인지 검사하는 함수.
bool validId(const std::string& value) {
    return !value.empty() && value.size() <= 160 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}
// 잘못된 숫자와 과도한 입력값을 효과 실행 전에 차단하는 함수.
bool validPayload(const InteractionRequest& request) {
    if (request.point && (!std::isfinite(request.point->x) || !std::isfinite(request.point->y))) return false;
    if (request.values.size() > 32) return false;
    for (const auto& [key, value] : request.values) {
        if (!validId(key)) return false;
        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number)) return false;
        if (const auto* text = std::get_if<std::string>(&value); text && text->size() > 1024) return false;
    }
    return (request.context.behaviorId.empty() || validId(request.context.behaviorId)) && request.context.reactionId.size() <= 256;
}
// 개별 효과 처리기의 예외가 전체 입력 처리를 중단하지 않도록 오류 값으로 바꾸는 함수.
Result<InteractionReaction> execute(const InteractionManager::Handler& handler, const InteractionRequest& request) {
    try { return handler(request); }
    catch (...) { return Result<InteractionReaction>::failure({"interaction.handler_failed", "Interaction handler failed."}); }
}
}
// 종류별 처리기를 등록하고 중복으로 덮어쓰지 않는 함수.
Result<void> InteractionManager::registerType(InteractionType type, Handler handler) {
    if (!validId(type.id) || !validId(type.sourceId) || type.name.empty() || !handler ||
        (type.mode != InteractionMode::Once && type.mode != InteractionMode::Continuous))
        return Result<void>::failure({"interaction.invalid_type", "Invalid interaction type."});
    if (types_.contains(type.id)) return Result<void>::failure({"interaction.duplicate_type", "Interaction type is already registered."});
    const auto id = type.id;
    types_.emplace(id, Entry{std::move(type), std::move(handler)});
    return Result<void>::success();
}
// 하나의 대상을 숨기거나 제거할 때만 그 대상의 입력을 취소하는 함수.
void InteractionManager::setTargetAvailable(const std::string& id, bool available) {
    if (!validId(id)) return;
    auto& target = targets_[id];
    target.available = available;
    if (!available) cancel(id);
}
// 종류를 비활성화해도 다른 기능의 세션은 유지하는 함수.
Result<void> InteractionManager::setEnabled(const std::string& typeId, bool enabled) {
    const auto found = types_.find(typeId);
    if (found == types_.end()) return Result<void>::failure({"interaction.unknown_type", "Unknown interaction type."});
    found->second.enabled = enabled;
    if (!enabled) for (const auto& [id, target] : targets_)
        if (target.session && target.session->typeId == typeId) cancel(id);
    return Result<void>::success();
}
// 마지막 입력으로 취소를 전달하고 세션을 종료하는 함수. 완료한 효과는 되돌리지 않는다.
void InteractionManager::cancel(const std::string& targetId) {
    const auto found = targets_.find(targetId);
    if (found == targets_.end() || !found->second.session) return;
    auto request = *found->second.session;
    found->second.session.reset();
    request.phase = InteractionPhase::Cancel;
    (void)execute(types_.at(request.typeId).handler, request);
}
// 새 요청의 식별자와 입력 단계를 검사한 뒤 등록된 기능에만 전달하는 함수.
InteractionOutcome InteractionManager::submit(const InteractionRequest& input) {
    const auto reject = [&input](std::string reason) {
        return InteractionOutcome{input.instanceId, InteractionStatus::Rejected, std::move(reason), std::nullopt};
    };
    const auto type = types_.find(input.typeId);
    if (type == types_.end()) return reject("interaction.unknown_type");
    if (!type->second.enabled) return reject("interaction.disabled");
    const auto target = targets_.find(input.targetId);
    if (target == targets_.end() || !target->second.available) return reject("interaction.target_unavailable");
    if (!input.instanceId || !validPayload(input)) return reject("interaction.invalid_input");
    auto& state = target->second;
    const bool starts = input.phase == InteractionPhase::Invoke || input.phase == InteractionPhase::Begin;
    if ((type->second.type.mode == InteractionMode::Once && input.phase != InteractionPhase::Invoke) ||
        (type->second.type.mode == InteractionMode::Continuous && input.phase == InteractionPhase::Invoke))
        return reject("interaction.invalid_phase");
    auto request = input;
    if (starts) {
        if (state.session) return reject("interaction.target_busy");
        if (input.instanceId <= state.lastStarted) return reject("interaction.stale_request");
        state.lastStarted = input.instanceId;
    } else {
        if (input.phase != InteractionPhase::Update && input.phase != InteractionPhase::End && input.phase != InteractionPhase::Cancel)
            return reject("interaction.invalid_phase");
        if (!state.session || state.session->instanceId != input.instanceId || state.session->typeId != input.typeId)
            return reject("interaction.no_session");
        request.context = state.session->context;
    }
    if (input.phase == InteractionPhase::Begin) state.session = request;
    const auto effect = execute(type->second.handler, request);
    if (!effect) {
        if (input.phase != InteractionPhase::Invoke) cancel(input.targetId);
        return reject(effect.error().code);
    }
    const auto& reaction = effect.value();
    const bool finished = input.phase == InteractionPhase::Invoke || input.phase == InteractionPhase::End;
    const bool cancelled = input.phase == InteractionPhase::Cancel;
    if ((!reaction.cueId.empty() && (!validId(reaction.cueId) || ((finished || cancelled) && reaction.durationMs == 0))) ||
        reaction.durationMs < 0 || reaction.durationMs > 60000) {
        if (input.phase != InteractionPhase::Invoke) cancel(input.targetId);
        return reject("interaction.invalid_reaction");
    }
    if (finished || cancelled) state.session.reset();
    else state.session = request;
    return {input.instanceId, cancelled ? InteractionStatus::Cancelled : finished ? InteractionStatus::Completed : InteractionStatus::Accepted,
        {}, reaction.cueId.empty() ? std::nullopt : std::optional<InteractionReaction>(reaction)};
}
// 기본 종류를 등록하며 업무 실행이나 창 이동은 직접 수행하지 않는 함수.
Result<void> registerBasicCompanionInteractions(InteractionManager& manager) {
    auto result = manager.registerType({"companion.click", "companion.basic", "클릭", InteractionMode::Once},
        [](const InteractionRequest& request) {
            // 현재 상황에 맞는 짧은 클릭 반응만 만드는 함수.
            return Result<InteractionReaction>::success({"interact.click", request.context.behaviorId == "working"
                ? "작업은 계속하고 있어요!" : "불렀어요?", 1500});
        });
    if (!result) return result;
    result = manager.registerType({"companion.drag", "companion.basic", "끌기", InteractionMode::Continuous},
        [](const InteractionRequest& request) {
            // 이동 중 반응과 이동 종료 반응을 구분하는 함수.
            if (request.phase == InteractionPhase::Cancel) return Result<InteractionReaction>::success({});
            return Result<InteractionReaction>::success({"interact.drag", request.phase == InteractionPhase::End ? "여기로 왔어요." : "어디로 갈까요?",
                request.phase == InteractionPhase::End ? 1000 : 0});
        });
    if (!result) return result;
    return manager.registerType({"companion.pet", "companion.basic", "쓰다듬기", InteractionMode::Continuous},
        [](const InteractionRequest& request) {
            // 접촉점이 있는 쓰다듬기 요청에만 반응하는 함수.
            if (request.phase == InteractionPhase::Cancel) return Result<InteractionReaction>::success({});
            if (!request.point) return Result<InteractionReaction>::failure({"interaction.point_required", "A contact point is required."});
            return Result<InteractionReaction>::success({"interact.pet", "쓰다듬어 주는 거예요?", request.phase == InteractionPhase::End ? 1000 : 0});
        });
}
}
