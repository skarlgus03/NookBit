#include "application/use_cases/verify_saved_plan_use_case.hpp"

#include "application/use_cases/content_fingerprint.hpp"
#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>

#include <algorithm>

namespace nookbit {

// 이력 읽기와 현재 상태 재검사에 필요한 기능을 받는 생성자.
VerifySavedPlanUseCase::VerifySavedPlanUseCase(IRuleSetRepository& rules,
                                               IScanner& scanner,
                                               IPlanner& planner,
                                               IPlanHasher& hasher,
                                               IHistoryRepository& history, ITransferVolumeInspector* volumes)
    : rules_(rules), scanner_(scanner), planner_(planner),
      hasher_(hasher), history_(history), volumes_(volumes) {}

// DB에서 계획을 다시 읽고 해시와 현재 계획을 비교하는 함수.
Result<OperationPlan> VerifySavedPlanUseCase::execute(
    const PlanId& planId,
    const std::vector<std::filesystem::path>& selectedFiles,
    const UserSettings& settings,
    PlanStatus expectedStatus) {
    if (planId.empty() || selectedFiles.empty()) {
        return Result<OperationPlan>::failure({
            "verify.input_invalid", "A saved plan and its selected files are required.",
        });
    }
    auto loaded = history_.incompletePlans();
    if (!loaded) return Result<OperationPlan>::failure(loaded.error());
    const auto found = std::find_if(loaded.value().begin(), loaded.value().end(),
                                    [&planId](const OperationPlan& plan) {
                                        return plan.id == planId;
                                    });
    if (found == loaded.value().end()) {
        return Result<OperationPlan>::failure({
            "verify.plan_missing", "The saved plan could not be found.",
        });
    }
    const auto& saved = *found;
    const auto format = PlanSignature::validateFormat(saved);
    if (!format) return Result<OperationPlan>::failure(format.error());
    const auto scope = PlanSignature::validateMovementScope(saved);
    if (!scope) return Result<OperationPlan>::failure(scope.error());
    if (saved.status != expectedStatus ||
        !PlanSignature::isSha256Hex(saved.planHash) ||
        saved.planHash != hasher_.sha256(PlanSignature::approvalPayload(saved))) {
        return Result<OperationPlan>::failure({
            "verify.plan_modified", "The saved plan is not an intact unapproved plan.",
        });
    }

    if (saved.formatVersion == 2 && !PlanSignature::isMetadataRenamePlan(saved)) {
        if (!volumes_) return Result<OperationPlan>::failure({"verify.volume_verifier_required", "V2 approval requires current volume verification."});
        PrepareTransferPlanUseCase prepare(*volumes_);
        const auto checked = prepare.verify(saved);
        if (!checked) return Result<OperationPlan>::failure(checked.error());
    }

    if (saved.settingsRevision != hasher_.sha256(PlanSignature::settings(settings))) {
        return Result<OperationPlan>::failure({
            "verify.settings_changed", "Settings changed after the plan was saved.",
        });
    }
    auto rules = rules_.load();
    if (!rules) return Result<OperationPlan>::failure(rules.error());
    if (saved.ruleSetVersion != rules.value().version ||
        saved.rulesRevision != hasher_.sha256(PlanSignature::rules(rules.value()))) {
        return Result<OperationPlan>::failure({
            "verify.rules_changed", "Rules changed after the plan was saved.",
        });
    }

    if (PlanSignature::isMetadataRenamePlan(saved)) {
        // 경로만 고정하고 개별 파일의 접근/객체/이동 실패는 실제 시도에서 처리한다.
        if (selectedFiles.size() != saved.steps.size() || std::any_of(saved.steps.begin(), saved.steps.end(), [&](const MoveStep& step) {
            return std::find(selectedFiles.begin(), selectedFiles.end(), step.source) == selectedFiles.end();
        })) return Result<OperationPlan>::failure({"verify.selection_changed", "Selected paths differ from the frozen metadata plan."});
        return Result<OperationPlan>::success(saved);
    }
    auto snapshot = scanner_.scan({{}, false, {}, selectedFiles});
    if (!snapshot) return Result<OperationPlan>::failure(snapshot.error());
    auto current = planner_.buildPlan(snapshot.value(), settings, rules.value());
    if (!current) return Result<OperationPlan>::failure(current.error());
    if (saved.formatVersion == 2 && current.value().formatVersion == 1) {
        PrepareTransferPlanUseCase prepare(*volumes_);
        current = prepare.execute(current.value());
        if (!current) return Result<OperationPlan>::failure(current.error());
    }
    inheritMetadataRenamePolicy(current.value(), saved);
    const auto currentFormat = PlanSignature::validateFormat(current.value());
    if (!currentFormat) return Result<OperationPlan>::failure(currentFormat.error());
    auto hashed = attachContentHashes(current.value(), scanner_, hasher_);
    if (!hashed) return Result<OperationPlan>::failure(hashed.error());
    if (PlanSignature::planContents(saved) != PlanSignature::planContents(current.value())) {
        return Result<OperationPlan>::failure({
            "verify.plan_stale", "Files or proposed destinations changed after the plan was saved.",
        });
    }
    return Result<OperationPlan>::success(saved);
}

} // namespace nookbit
