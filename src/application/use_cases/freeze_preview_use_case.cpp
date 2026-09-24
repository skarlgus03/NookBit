#include "application/use_cases/freeze_preview_use_case.hpp"

#include "application/use_cases/plan_signature.hpp"

#include <algorithm>

namespace nookbit {

// 재검사·해시·저장에 필요한 외부 기능을 받는 생성자.
FreezePreviewUseCase::FreezePreviewUseCase(ISettingsRepository& settings,
                                           IRuleSetRepository& rules,
                                           IScanner& scanner,
                                           IPlanner& planner,
                                           IPlanHasher& hasher,
                                           IHistoryRepository& history)
    : settings_(settings), rules_(rules), scanner_(scanner), planner_(planner),
      hasher_(hasher), history_(history) {}

// 화면 초안과 현재 상태가 같을 때만 Planned 계획으로 저장하는 함수.
Result<OperationPlan> FreezePreviewUseCase::execute(
    const OperationPlan& draft,
    const std::vector<std::filesystem::path>& selectedFiles) {
    if (draft.status != PlanStatus::Draft || !draft.planHash.empty() ||
        draft.id.empty() || draft.settingsRevision.empty() ||
        draft.rulesRevision.empty() || selectedFiles.empty()) {
        return Result<OperationPlan>::failure({
            "freeze.invalid_draft", "Only a versioned, unapproved preview can be frozen.",
        });
    }

    auto settings = settings_.load();
    if (!settings) return Result<OperationPlan>::failure(settings.error());
    const auto currentRevision = hasher_.sha256(PlanSignature::settings(settings.value()));
    if (currentRevision != draft.settingsRevision) {
        return Result<OperationPlan>::failure({
            "freeze.settings_changed", "Settings changed after the preview was created.",
        });
    }

    auto rules = rules_.load();
    if (!rules) return Result<OperationPlan>::failure(rules.error());
    if (rules.value().version != draft.ruleSetVersion) {
        return Result<OperationPlan>::failure({
            "freeze.rules_changed", "The rule-set version changed after the preview.",
        });
    }
    const auto currentRulesRevision = hasher_.sha256(PlanSignature::rules(rules.value()));
    if (currentRulesRevision != draft.rulesRevision) {
        return Result<OperationPlan>::failure({
            "freeze.rules_changed", "Rules changed after the preview was created.",
        });
    }

    auto snapshot = scanner_.scan({{}, false, {}, selectedFiles});
    if (!snapshot) return Result<OperationPlan>::failure(snapshot.error());
    auto refreshed = planner_.buildPlan(snapshot.value(), settings.value(), rules.value());
    if (!refreshed) return Result<OperationPlan>::failure(refreshed.error());
    if (PlanSignature::planContents(draft) != PlanSignature::planContents(refreshed.value())) {
        return Result<OperationPlan>::failure({
            "freeze.preview_stale", "Files or proposed destinations changed after the preview.",
        });
    }
    if (std::none_of(draft.steps.begin(), draft.steps.end(), [](const MoveStep& step) {
            return step.status == StepStatus::Planned;
        })) {
        return Result<OperationPlan>::failure({
            "freeze.no_steps", "There are no classified file operations to save.",
        });
    }

    OperationPlan frozen = draft;
    frozen.status = PlanStatus::Planned;
    frozen.planHash = hasher_.sha256(PlanSignature::approvalPayload(frozen));
    if (!PlanSignature::isSha256Hex(frozen.planHash)) {
        return Result<OperationPlan>::failure({
            "freeze.hash_failed", "The plan digest was not a SHA-256 hex string.",
        });
    }
    auto saved = history_.savePlan(frozen);
    if (!saved) return Result<OperationPlan>::failure(saved.error());
    return Result<OperationPlan>::success(std::move(frozen));
}

} // namespace nookbit
