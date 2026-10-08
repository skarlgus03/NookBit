#pragma once

#include <nookbit/application/ports.hpp>
#include "modules/watch/watch_file_readiness.hpp"

namespace nookbit {

/// 감시 폴더의 파일 검사와 규칙·조건 판단을 조율하는 유스케이스.
class EvaluateWatchFolderUseCase final : public IWatchProposalUseCase {
public:
    /// 파일 검사·규칙 판단·규칙 조회 계약을 주입받는 생성자.
    EvaluateWatchFolderUseCase(IScanner& scanner,
                               IRuleEngine& ruleEngine,
                               IRuleSetRepository& rules,
                               int stableWindowMs = 30000);

    /// 대기 경로를 제외하고 감시 폴더의 분류 가능한 파일로 제안 조건을 판단하는 함수.
    [[nodiscard]] Result<WatchProposal> execute(
        const WatchSettings& settings, std::int64_t nowUtcMs,
        const std::vector<std::filesystem::path>& pendingPaths = {}) override;

private:
    IScanner& scanner_;
    IRuleEngine& ruleEngine_;
    IRuleSetRepository& rules_;
    WatchFileReadiness readiness_;
    std::filesystem::path observedFolder_;
    bool observedRecursive_{false};
};

} // namespace nookbit
