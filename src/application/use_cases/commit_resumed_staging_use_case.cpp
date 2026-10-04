#include <nookbit/application/commit_resumed_staging_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {
// 외부 소유의 일꾼을 생성하지 않고 참조로 받는 함수.
CommitResumedStagingUseCase::CommitResumedStagingUseCase(const IHistoryRepository& history,
    ITransferJournal& journal, IStagedCopy& copy, const IPlanHasher& hasher)
    : history_(history), journal_(journal), copy_(copy), hasher_(hasher) {}

// 원본을 유지한 채 검증된 임시 객체의 이름과 확정 기록만 변경하는 함수.
Result<TransferAttempt> CommitResumedStagingUseCase::execute(const std::string& attemptId,
    const std::function<bool()>& cancelled) {
    using Return = Result<TransferAttempt>;
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return loaded;
    auto state = loaded.value();
    auto eligible = state;
    if (eligible.stagingCommit == StagingCommit::IntentRecorded) eligible.stagingCommit = StagingCommit::None;
    const auto intent = TransferValidation::recordStagingCommit(eligible, StagingCommit::IntentRecorded);
    if (!intent) return intent;
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, state);
    if (!binding) return Return::failure(binding.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Commit cancelled before mutation"});
    const auto& movement = binding.value().movement;
    auto session = copy_.openVerified(state, movement ? &*movement : nullptr, cancelled);
    if (!session) return Return::failure(session.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Commit cancelled before intent"});
    const auto savedIntent = journal_.recordStagingCommit(state, binding.value().planHash, StagingCommit::IntentRecorded);
    if (!savedIntent) return Return::failure(savedIntent.error());
    state = intent.value();
    // 의도 이후에는 취소로 결과 기록을 생략하지 않는다.
    const auto committed = session.value()->commitNoReplace(state.destination);
    if (!committed) return Return::failure(committed.error());
    const auto saved = journal_.recordStagingCommit(state, binding.value().planHash, StagingCommit::Committed, committed.value());
    if (!saved) return Return::failure(saved.error());
    return TransferValidation::recordStagingCommit(state, StagingCommit::Committed, committed.value());
}
}
