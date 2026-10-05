#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>

#include <utility>

namespace nookbit {

// 미리보기 생성에 필요한 외부 기능을 저장하는 생성자.
CreatePreviewUseCase::CreatePreviewUseCase(
    ISettingsRepository& settings,
    IRuleSetRepository& rules,
    IScanner& scanner,
    IPlanner& planner,
    IPlanHasher* hasher, ITransferVolumeInspector* volumes)
    : settings_(&settings), rules_(rules), scanner_(scanner), planner_(planner), hasher_(hasher), volumes_(volumes) {}

// 요청에 포함된 목적지를 사용할 때 외부 일꾼만 저장하는 생성자.
CreatePreviewUseCase::CreatePreviewUseCase(
    IRuleSetRepository& rules,
    IScanner& scanner,
    IPlanner& planner,
    IPlanHasher* hasher, ITransferVolumeInspector* volumes)
    : settings_(nullptr), rules_(rules), scanner_(scanner), planner_(planner), hasher_(hasher), volumes_(volumes) {}

// 대기 파일과 요청 목적지·규칙을 확인한 뒤 이동 계획을 생성하는 함수.
Result<OperationPlan> CreatePreviewUseCase::execute(
    const CreatePreviewRequest& request) {
    if (request.ruleSetVersion.empty() && request.destinationRoot.empty()) {
        return Result<OperationPlan>::failure({
            "preview.rule_set_version_empty",
            "A rule-set version is required to create a preview.",
        });
    }
    if (request.selectedFiles.empty()) {
        return Result<OperationPlan>::failure({
            "preview.selection_empty",
            "At least one selected file is required to create a preview.",
        });
    }
    if (!settings_ && request.destinationRoot.empty()) {
        return Result<OperationPlan>::failure({
            "preview.destination_root_empty",
            "A destination root is required when settings are not provided.",
        });
    }

    UserSettings settings;
    if (settings_) {
        auto loaded = settings_->load();
        if (!loaded) return Result<OperationPlan>::failure(loaded.error());
        settings = std::move(loaded.value());
    }
    if (!request.destinationRoot.empty()) settings.homeRoot = request.destinationRoot;
    if (request.defaultCollisionPolicy) {
        settings.defaultCollisionPolicy = *request.defaultCollisionPolicy;
    }

    auto rules = rules_.load();
    if (!rules) {
        return Result<OperationPlan>::failure(rules.error());
    }
    if (!request.ruleSetVersion.empty() &&
        rules.value().version != request.ruleSetVersion) {
        return Result<OperationPlan>::failure({
            "preview.rule_set_version_mismatch",
            "The requested rule-set version differs from the loaded rules.",
        });
    }

    ScanRequest scanRequest{{}, false, {}, request.selectedFiles};

    auto snapshot = scanner_.scan(scanRequest);
    if (!snapshot) {
        return Result<OperationPlan>::failure(snapshot.error());
    }

    auto plan = planner_.buildPlan(
        snapshot.value(),
        settings,
        rules.value());
    if (plan && volumes_) {
        PrepareTransferPlanUseCase prepare(*volumes_);
        plan = prepare.execute(plan.value());
    }
    if (plan) {
        const auto scope = PlanSignature::validateMovementScope(plan.value());
        if (!scope) return Result<OperationPlan>::failure(scope.error());
    }
    if (plan && hasher_) {
        plan.value().settingsRevision = hasher_->sha256(
            PlanSignature::settings(settings));
        plan.value().rulesRevision = hasher_->sha256(
            PlanSignature::rules(rules.value()));
    }
    return plan;
}

} // namespace nookbit
