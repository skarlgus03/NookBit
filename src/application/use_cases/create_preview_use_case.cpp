#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>

#include <utility>
#include <algorithm>
#include <chrono>
#include <set>
#include <cwctype>
#include "modules/rules/processing_flow.hpp"

namespace nookbit {

// 미리보기 생성에 필요한 외부 기능을 저장하는 생성자.
CreatePreviewUseCase::CreatePreviewUseCase(
    ISettingsRepository& settings,
    IRuleSetRepository& rules,
    IScanner& scanner,
    IPlanner& planner,
    IPlanHasher* hasher, ITransferVolumeInspector* volumes, IProcessingFlowRouter* flows)
    : settings_(&settings), rules_(rules), scanner_(scanner), planner_(planner), hasher_(hasher), volumes_(volumes), flows_(flows) {}

// 요청에 포함된 목적지를 사용할 때 외부 일꾼만 저장하는 생성자.
CreatePreviewUseCase::CreatePreviewUseCase(
    IRuleSetRepository& rules,
    IScanner& scanner,
    IPlanner& planner,
    IPlanHasher* hasher, ITransferVolumeInspector* volumes, IProcessingFlowRouter* flows)
    : settings_(nullptr), rules_(rules), scanner_(scanner), planner_(planner), hasher_(hasher), volumes_(volumes), flows_(flows) {}

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
    if (request.allowedDestinationRoots) settings.allowedDestinationRoots = *request.allowedDestinationRoots;
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

    const auto flowValidation = validateProcessingFlows(rules.value());
    if (!flowValidation) return Result<OperationPlan>::failure(flowValidation.error());
    ScanRequest scanRequest{{}, false, {}, request.selectedFiles};
    for (const auto& selection : request.folderSelections) {
        if (selection.folderFlowId.empty()) continue;
        const auto& active = rules.value();
        const auto flow = std::find_if(active.folderFlows.begin(), active.folderFlows.end(),
            [&](const RuleSet::FolderFlow& item) { return item.id == selection.folderFlowId; });
        if (active.schemaVersion != 3 || selection.ruleSetId != active.id || flow == active.folderFlows.end() ||
            std::find(request.selectedFiles.begin(), request.selectedFiles.end(), selection.path) == request.selectedFiles.end())
            return Result<OperationPlan>::failure({"preview.folder_flow_invalid", "Selected folder flow is unavailable in the active rules. Select it again."});
        if (!flows_) {
            if (!flow->nodes.empty()) return Result<OperationPlan>::failure({"preview.flow_router_missing", "Custom processing flows require a flow router."});
            scanRequest.expandFolders.push_back(selection.path);
        }
    }

    auto snapshot = scanner_.scan(scanRequest);
    if (!snapshot) {
        return Result<OperationPlan>::failure(snapshot.error());
    }
    if (flows_ && !request.folderSelections.empty()) {
        auto pending = std::move(snapshot.value().files);
        snapshot.value().files.clear();
        for (auto& file : pending) for (const auto& selection : request.folderSelections) {
            if (selection.path == file.absolutePath && !selection.folderFlowId.empty()) {
                if (!file.fingerprint.directory) return Result<OperationPlan>::failure({"preview.flow_input_invalid", "Only queued folders can select a folder flow."});
                const auto flow = std::find_if(rules.value().folderFlows.begin(), rules.value().folderFlows.end(), [&](const auto& item) { return item.id == selection.folderFlowId; });
                if (!flow->folderInput) return Result<OperationPlan>::failure({"preview.flow_input_invalid", "Only folder-input flows appear in the queue."});
                file.processingFlowId = selection.folderFlowId;
            }
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        for (std::size_t index = 0; index < pending.size(); ++index) {
            auto file = std::move(pending[index]);
            if (!file.processingFlowId.empty() && file.fingerprint.directory) {
                const auto route = flows_->route(file, rules.value(), now);
                if (!route) return Result<OperationPlan>::failure(route.error());
                if (route.value().traverse) {
                    ScanRequest traversal;
                    traversal.roots = {file.absolutePath};
                    traversal.recursive = route.value().recursive;
                    traversal.maxDepth = route.value().maxDepth;
                    const auto contents = scanner_.scan(traversal);
                    if (!contents) return Result<OperationPlan>::failure(contents.error());
                    if (!contents.value().warnings.empty()) return Result<OperationPlan>::failure({"preview.flow_scan_incomplete", "Folder traversal was incomplete; no files were moved."});
                    for (auto child : contents.value().files) {
                        child.processingFlowId = route.value().flowId;
                        child.processingAfterNode = route.value().nodeId;
                        pending.push_back(std::move(child));
                    }
                    continue;
                }
            }
            snapshot.value().files.push_back(std::move(file));
        }
        // 하나의 실제 원본이 두 흐름으로 계획되거나 부모와 자식이 함께 이동하지 않게 한다.
        std::set<std::wstring> sources;
        std::vector<std::wstring> folders;
        for (const auto& file : snapshot.value().files) {
            auto key = file.absolutePath.lexically_normal().wstring();
#ifdef _WIN32
            std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
#endif
            if (!sources.insert(key).second) return Result<OperationPlan>::failure({"preview.flow_overlap", "Duplicate move sources."});
            if (file.fingerprint.directory) folders.push_back(key);
        }
        for (const auto& folder : folders) {
            const auto prefix = folder + std::wstring(1, std::filesystem::path::preferred_separator);
            const auto child = sources.lower_bound(prefix);
            if (child != sources.end() && child->starts_with(prefix))
                return Result<OperationPlan>::failure({"preview.flow_overlap", "A folder and its contents cannot move together."});
        }
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
