#include "modules/character/behavior_manager.hpp"
#include <algorithm>
#include <utility>

namespace nookbit {
namespace {
// 식별자와 태그가 빈 값이나 제어 문자를 포함하지 않는지 검사하는 함수.
bool valid(const std::string& value) {
    return !value.empty() && value.size() <= 160 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}
// 상황 태그 집합을 검증하는 함수.
bool validTags(const std::set<std::string>& tags) {
    return tags.size() <= 32 && std::all_of(tags.begin(), tags.end(), valid);
}
}
// 중복 행동과 서로 모순되는 조건을 차단하는 함수.
Result<void> BehaviorManager::registerBehavior(CompanionBehavior behavior) {
    if (!valid(behavior.id) || !valid(behavior.cueId) || !validTags(behavior.requiredTags) || !validTags(behavior.blockedTags))
        return Result<void>::failure({"behavior.invalid_definition", "Invalid behavior definition."});
    for (const auto& tag : behavior.requiredTags) if (behavior.blockedTags.contains(tag))
        return Result<void>::failure({"behavior.conflicting_tags", "A tag cannot be required and blocked."});
    for (const auto& item : behaviors_) if (item.id == behavior.id)
        return Result<void>::failure({"behavior.duplicate_id", "Behavior is already registered."});
    behaviors_.push_back(std::move(behavior));
    return Result<void>::success();
}
// 독립적인 발신자의 상황을 보존하며 하나의 입력만 교체하는 함수.
Result<void> BehaviorManager::publish(const BehaviorUpdate& update) {
    if (!valid(update.sourceId) || !valid(update.entryId) || !validTags(update.tags))
        return Result<void>::failure({"behavior.invalid_update", "Invalid behavior input."});
    const auto key = std::make_pair(update.sourceId, update.entryId);
    if (update.active) inputs_[key] = update.tags;
    else inputs_.erase(key);
    return Result<void>::success();
}
// 현재 상황을 매번 다시 평가하여 종료된 작업 모습으로 복귀하지 않게 하는 함수.
std::optional<CompanionBehavior> BehaviorManager::current() const {
    std::set<std::string> tags;
    for (const auto& [key, values] : inputs_) tags.insert(values.begin(), values.end());
    const CompanionBehavior* selected = nullptr;
    for (const auto& behavior : behaviors_) {
        const bool required = std::all_of(behavior.requiredTags.begin(), behavior.requiredTags.end(), [&tags](const auto& tag) { return tags.contains(tag); });
        const bool blocked = std::any_of(behavior.blockedTags.begin(), behavior.blockedTags.end(), [&tags](const auto& tag) { return tags.contains(tag); });
        if (required && !blocked && (!selected || behavior.priority > selected->priority)) selected = &behavior;
    }
    return selected ? std::optional<CompanionBehavior>(*selected) : std::nullopt;
}
}
