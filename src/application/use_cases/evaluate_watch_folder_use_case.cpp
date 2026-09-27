#include "application/use_cases/evaluate_watch_folder_use_case.hpp"

#include "modules/watch/watch_evaluator.hpp"

#include <utility>

namespace nookbit {

// 파일 검사·규칙 판단·규칙 조회 계약을 주입받는 생성자.
EvaluateWatchFolderUseCase::EvaluateWatchFolderUseCase(
    IScanner& scanner, IRuleEngine& ruleEngine, IRuleSetRepository& rules)
    : scanner_(scanner), ruleEngine_(ruleEngine), rules_(rules) {}

// 감시 폴더를 읽고 분류 가능한 파일의 제안 조건을 판단하는 함수.
Result<WatchProposal> EvaluateWatchFolderUseCase::execute(
    const WatchSettings& settings, std::int64_t nowUtcMs) {
    if (settings.folder.empty() || settings.conditions.empty()) {
        return Result<WatchProposal>::failure({
            "watch.settings_incomplete", "A folder and at least one condition are required.",
        });
    }
    ScanRequest request;
    request.roots = {settings.folder};
    request.recursive = settings.recursive;
    request.excludePatterns = settings.excludePatterns;
    auto scanned = scanner_.scan(request);
    if (!scanned) return Result<WatchProposal>::failure(scanned.error());
    auto rules = rules_.load();
    if (!rules) return Result<WatchProposal>::failure(rules.error());

    WatchProposal proposal;
    proposal.warnings = std::move(scanned.value().warnings);
    proposal.eligibleFiles.reserve(scanned.value().files.size());
    for (const auto& file : scanned.value().files) {
        auto decision = ruleEngine_.decide(
            file, rules.value(), MultiMatchPolicy::FirstMatch, nowUtcMs);
        if (!decision) return Result<WatchProposal>::failure(decision.error());
        if (decision.value()) proposal.eligibleFiles.push_back(file);
    }
    auto evaluated = WatchEvaluator::evaluate(settings, proposal.eligibleFiles, nowUtcMs);
    if (!evaluated) return Result<WatchProposal>::failure(evaluated.error());
    proposal.evaluation = evaluated.value();
    return Result<WatchProposal>::success(std::move(proposal));
}

} // namespace nookbit
