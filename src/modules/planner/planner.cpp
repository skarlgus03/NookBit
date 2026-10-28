#include "modules/planner/planner.hpp"

#include "modules/planner/collision_resolver.hpp"
#include "modules/planner/path_safety_validator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace nookbit {
namespace {

// 경고에 사용할 경로를 UTF-8 문자열로 변환하는 함수.
std::string utf8Path(const std::filesystem::path& path) {
    const auto characters = path.u8string();
    std::string result;
    result.reserve(characters.size());
    for (const char8_t character : characters) result.push_back(static_cast<char>(character));
    return result;
}

// Windows에서 대소문자만 다른 목적지를 같은 경로로 비교하는 함수.
std::wstring pathKey(const std::filesystem::path& path) {
    auto key = path.lexically_normal().wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
#endif
    return key;
}

// 규칙 판단 결과의 목적지가 상대 경로 범위 안에 있는지 확인하는 함수.
bool isSafeRelativeDestination(const std::filesystem::path& destination) {
    if (destination.empty() || destination.has_root_path()) return false;
    for (const auto& component : destination) {
        if (component == L"..") return false;
    }
    return true;
}

// 파일 시스템과 앞선 계획 단계의 목적지 충돌 여부를 확인하는 함수.
bool destinationExists(const std::filesystem::path& path,
                       const std::set<std::wstring>& reserved,
                       std::error_code& error) {
    if (reserved.contains(pathKey(path))) return true;
    return std::filesystem::exists(path, error);
}

// 새 미리보기 초안에 사용할 고유 식별자를 만드는 함수.
std::string nextPlanId() {
    static std::atomic<std::uint64_t> counter{0};
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "preview-" + std::to_string(nowMs) + "-" + std::to_string(++counter);
}

} // namespace

// 규칙 판단기와 보호할 앱 데이터 폴더를 받는 생성자.
Planner::Planner(IRuleEngine& ruleEngine, std::filesystem::path appDataRoot)
    : Planner(ruleEngine, std::move(appDataRoot), CrossVolumeDrafts::Reject) {}

// 통합 백엔드에서만 다른 드라이브 초안을 만들도록 정책을 받는 생성자.
Planner::Planner(IRuleEngine& ruleEngine, std::filesystem::path appDataRoot, CrossVolumeDrafts crossVolumeDrafts)
    : ruleEngine_(ruleEngine), appDataRoot_(std::move(appDataRoot)), crossVolumeDrafts_(crossVolumeDrafts) {}

// 파일별 분류와 안전 검사를 수행하되 실제 파일은 변경하지 않는 함수.
Result<OperationPlan> Planner::buildPlan(
    const ScanSnapshot& snapshot,
    const UserSettings& settings,
    const RuleSet& rules) {
    if (snapshot.files.empty()) {
        return Result<OperationPlan>::failure({
            "plan.files_empty", "No scanned files are available for a preview.",
        });
    }
    if (rules.version.empty() || !settings.homeRoot.is_absolute()) {
        return Result<OperationPlan>::failure({
            "plan.input_invalid", "A rule version and absolute organizer home are required.",
        });
    }
    if (!appDataRoot_.is_absolute()) {
        return Result<OperationPlan>::failure({
            "plan.app_data_root_missing", "An absolute application-data root is required for path safety.",
        });
    }

    OperationPlan plan;
    plan.id = nextPlanId();
    plan.ruleSetVersion = rules.version;
    plan.status = PlanStatus::Draft;
    plan.warnings = snapshot.warnings;

    std::vector<std::filesystem::path> selectedFiles;
    selectedFiles.reserve(snapshot.files.size());
    for (const auto& file : snapshot.files) selectedFiles.push_back(file.absolutePath);

    std::set<std::wstring> reservedDestinations;
    const auto nowUtcMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& file : snapshot.files) {
        auto decision = ruleEngine_.decide(file, rules, settings.multiMatchPolicy, nowUtcMs);
        if (!decision) return Result<OperationPlan>::failure(decision.error());
        if (!decision.value()) {
            if (settings.unmatchedPolicy == UnmatchedPolicy::Abort) {
                return Result<OperationPlan>::failure({
                    "plan.unmatched_file", "A selected file has no matching classification rule.",
                });
            }
            plan.warnings.push_back({
                "plan.unmatched_file", "A selected file was left out of the preview: " +
                    utf8Path(file.absolutePath),
            });
            plan.previewOutcomes.push_back({file.absolutePath, std::nullopt, {},
                                            PreviewDisposition::Unmatched});
            continue;
        }

        const auto& chosen = *decision.value();
        if (chosen.excluded) {
            plan.warnings.push_back({
                "plan.excluded_file", "A selected file was explicitly excluded: " +
                    utf8Path(file.absolutePath),
            });
            plan.previewOutcomes.push_back({file.absolutePath, std::nullopt,
                                            chosen.matchedRuleId, PreviewDisposition::Excluded});
            continue;
        }
        if (chosen.absoluteDestination ? !chosen.destinationRelativePath.is_absolute()
                                       : !isSafeRelativeDestination(chosen.destinationRelativePath)) {
            return Result<OperationPlan>::failure({
                "plan.unsafe_destination", "A classification rule returned an unsafe destination.",
            });
        }
        const auto requestedDestination = (chosen.absoluteDestination
            ? chosen.destinationRelativePath : settings.homeRoot / chosen.destinationRelativePath)
            / file.absolutePath.filename();
        auto safety = PathSafetyValidator::validateMove(
            file.absolutePath, requestedDestination, selectedFiles, settings.homeRoot, appDataRoot_, settings.allowedDestinationRoots);
        if (!safety) return Result<OperationPlan>::failure(safety.error());
        // 통합 백엔드만 초안을 허용하며 실제 이동 방식은 후속 볼륨 계약 준비가 결정한다.
        if (crossVolumeDrafts_ == CrossVolumeDrafts::Reject &&
            pathKey(file.absolutePath.root_name()) != pathKey(requestedDestination.root_name())) {
            return Result<OperationPlan>::failure({
                "plan.cross_volume_unsupported", "Cross-volume file operations are not supported yet.",
            });
        }

        // 폴더끼리의 병합·자동 이름 변경은 하지 않고 해당 폴더만 제외한다.
        const auto policy = file.fingerprint.directory ? CollisionPolicy::Skip
            : chosen.collisionPolicy.value_or(settings.defaultCollisionPolicy);
        std::error_code existenceError;
        auto resolved = CollisionResolver::resolve(requestedDestination, policy,
            [&reservedDestinations, &existenceError](const std::filesystem::path& candidate) {
                return destinationExists(candidate, reservedDestinations, existenceError);
            });
        if (existenceError) {
            return Result<OperationPlan>::failure({
                "plan.destination_unavailable", "Cannot inspect a proposed destination.",
            });
        }
        if (!resolved && resolved.error().code != "collision.skip") {
            return Result<OperationPlan>::failure(resolved.error());
        }

        MoveStep step;
        step.id = plan.id + "-step-" + std::to_string(plan.steps.size() + 1);
        step.source = file.absolutePath;
        step.destination = resolved ? resolved.value() : requestedDestination;
        step.fingerprint = file.fingerprint;
        step.matchedRuleId = chosen.matchedRuleId;
        step.reason = chosen.reason;
        step.collisionPolicy = policy;
        step.status = resolved ? StepStatus::Planned : StepStatus::Skipped;
        if (resolved) {
            auto finalSafety = PathSafetyValidator::validateMove(
                step.source, step.destination, selectedFiles, settings.homeRoot, appDataRoot_, settings.allowedDestinationRoots);
            if (!finalSafety) return Result<OperationPlan>::failure(finalSafety.error());
            reservedDestinations.insert(pathKey(step.destination));
        }
        plan.previewOutcomes.push_back({
            step.source, step.destination, step.matchedRuleId,
            resolved ? PreviewDisposition::Planned : PreviewDisposition::CollisionSkipped,
        });
        plan.steps.push_back(std::move(step));
    }
    return Result<OperationPlan>::success(std::move(plan));
}

} // namespace nookbit
