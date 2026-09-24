#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/plan_signature.hpp"

namespace nookbit {

// 미리보기 생성에 필요한 외부 기능을 저장하는 생성자.
CreatePreviewUseCase::CreatePreviewUseCase(
    ISettingsRepository& settings,
    IRuleSetRepository& rules,
    IScanner& scanner,
    IPlanner& planner,
    IPlanHasher* hasher)
    : settings_(settings), rules_(rules), scanner_(scanner), planner_(planner), hasher_(hasher) {}

// 대기 파일 목록을 읽고 규칙 버전을 확인한 뒤 이동 계획을 생성하는 함수.
Result<OperationPlan> CreatePreviewUseCase::execute(
    const CreatePreviewRequest& request) {
    if (request.ruleSetVersion.empty()) {
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

    auto settings = settings_.load();
    if (!settings) {
        return Result<OperationPlan>::failure(settings.error());
    }

    auto rules = rules_.load();
    if (!rules) {
        return Result<OperationPlan>::failure(rules.error());
    }
    if (rules.value().version != request.ruleSetVersion) {
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
        settings.value(),
        rules.value());
    if (plan && hasher_) {
        plan.value().settingsRevision = hasher_->sha256(
            PlanSignature::settings(settings.value()));
        plan.value().rulesRevision = hasher_->sha256(
            PlanSignature::rules(rules.value()));
    }
    return plan;
}

} // namespace nookbit
