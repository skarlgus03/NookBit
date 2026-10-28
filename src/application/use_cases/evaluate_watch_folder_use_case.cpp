#include "application/use_cases/evaluate_watch_folder_use_case.hpp"

#include "modules/watch/watch_evaluator.hpp"

#include <utility>
#include <algorithm>
#include <cwctype>
#include <set>

namespace nookbit {
namespace {

// 경로 구분자·상대 구간과 Windows 대소문자를 정리해 비교 키를 만드는 함수.
std::wstring pendingPathKey(const std::filesystem::path& path) {
    auto key = path.lexically_normal().generic_wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
#endif
    return key;
}

} // namespace

// 감시용 검사와 외부 분류 조회 계약을 주입받는 생성자.
EvaluateWatchFolderUseCase::EvaluateWatchFolderUseCase(
    IScanner& scanner, IClassificationQuery& classification, int stableWindowMs)
    : scanner_(scanner), classification_(classification), readiness_(stableWindowMs) {}

// 감시 폴더를 읽고 분류 가능한 파일의 제안 조건을 판단하는 함수.
Result<WatchProposal> EvaluateWatchFolderUseCase::execute(
    const WatchSettings& settings, std::int64_t nowUtcMs,
    const std::vector<std::filesystem::path>& pendingPaths) {
    if (settings.folder.empty() || settings.conditions.empty()) {
        readiness_.reset();
        return Result<WatchProposal>::failure({
            "watch.settings_incomplete", "A folder and at least one condition are required.",
        });
    }
    const auto folder = settings.folder.lexically_normal();
    if (folder != observedFolder_ || settings.recursive != observedRecursive_) {
        readiness_.reset();
        observedFolder_ = folder;
        observedRecursive_ = settings.recursive;
    }
    ScanRequest request;
    request.roots = {settings.folder};
    request.recursive = settings.recursive;
    request.excludePatterns = settings.excludePatterns;
    auto scanned = scanner_.scan(request);
    if (!scanned) {
        readiness_.reset();
        return Result<WatchProposal>::failure(scanned.error());
    }
    WatchProposal proposal;
    proposal.warnings = std::move(scanned.value().warnings);
    std::vector<FileFact> candidates;
    candidates.reserve(scanned.value().files.size());
    std::set<std::wstring> pendingKeys;
    for (const auto& path : pendingPaths) pendingKeys.insert(pendingPathKey(path));
    for (const auto& file : scanned.value().files) {
        if (pendingKeys.contains(pendingPathKey(file.absolutePath))) continue;
        candidates.push_back(file);
    }
    const auto classified = classification_.classify(candidates, nowUtcMs);
    if (!classified) {
        readiness_.reset();
        return Result<WatchProposal>::failure(classified.error());
    }
    if (classified.value().rulesRevision.empty() || classified.value().files.size() != candidates.size()) {
        readiness_.reset();
        return Result<WatchProposal>::failure({"watch.classification_invalid", "Classification response does not match the requested files."});
    }
    proposal.rulesRevision = classified.value().rulesRevision;
    proposal.eligibleFiles.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& result = classified.value().files[index];
        if (result.source != candidates[index].absolutePath) {
            readiness_.reset();
            return Result<WatchProposal>::failure({"watch.classification_invalid", "Classification response changed file order or identity."});
        }
        if (result.decision && !result.decision->excluded) proposal.eligibleFiles.push_back(candidates[index]);
    }
    auto readiness = readiness_.observe(proposal.eligibleFiles, nowUtcMs);
    proposal.eligibleFiles = std::move(readiness.readyFiles);
    proposal.waitingFileCount = readiness.waitingCount;
    proposal.nextCheckDelayMs = readiness.nextCheckDelayMs;
    auto evaluated = WatchEvaluator::evaluate(settings, proposal.eligibleFiles, nowUtcMs);
    if (!evaluated) return Result<WatchProposal>::failure(evaluated.error());
    proposal.evaluation = evaluated.value();
    return Result<WatchProposal>::success(std::move(proposal));
}

} // namespace nookbit
