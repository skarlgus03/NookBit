#include "application/use_cases/classify_files_use_case.hpp"
#include "application/use_cases/plan_signature.hpp"
#include <utility>

namespace nookbit {
// 활성 규칙과 판단에 필요한 포트만 보관하는 생성자.
ClassifyFilesUseCase::ClassifyFilesUseCase(IRuleSetRepository& rules, IRuleEngine& engine, IPlanHasher& hasher)
    : rules_(rules), engine_(engine), hasher_(hasher) {}

// 규칙을 한 번 읽고 같은 규칙으로 모든 입력의 결과와 식별값을 돌려주는 함수.
Result<ClassificationSnapshot> ClassifyFilesUseCase::classify(
    const std::vector<FileFact>& files, std::int64_t nowUtcMs) const {
    const auto rules = rules_.load();
    if (!rules) return Result<ClassificationSnapshot>::failure(rules.error());
    ClassificationSnapshot snapshot;
    snapshot.rulesRevision = hasher_.sha256(PlanSignature::rules(rules.value()));
    if (!PlanSignature::isSha256Hex(snapshot.rulesRevision))
        return Result<ClassificationSnapshot>::failure({"classification.revision_invalid", "Cannot identify the active rule snapshot."});
    snapshot.files.reserve(files.size());
    for (const auto& file : files) {
        const auto decision = engine_.decide(file, rules.value(), MultiMatchPolicy::FirstMatch, nowUtcMs);
        if (!decision) return Result<ClassificationSnapshot>::failure(decision.error());
        snapshot.files.push_back({file.absolutePath, decision.value()});
    }
    return Result<ClassificationSnapshot>::success(std::move(snapshot));
}
}
