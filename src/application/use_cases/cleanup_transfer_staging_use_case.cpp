#include <nookbit/application/cleanup_transfer_staging_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {
// 외부에서 소유하는 기록·정리·승인 검증 포트를 빌려 받는 함수.
CleanupTransferStagingUseCase::CleanupTransferStagingUseCase(const IHistoryRepository& history,
    ITransferJournal& journal, IStagingCleanup& cleanup, const IPlanHasher& hasher)
    : history_(history), journal_(journal), cleanup_(cleanup), hasher_(hasher) {}

// 원본을 보호하고 임시 복사본 정리 의도 저장 뒤에만 삭제를 요청하는 함수.
Result<TransferAttempt> CleanupTransferStagingUseCase::execute(const std::string& attemptId) {
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return Result<TransferAttempt>::failure(loaded.error());
    auto state = loaded.value();
    const auto target = state.stagingCleanup == StagingCleanup::None ? StagingCleanup::IntentRecorded : StagingCleanup::Removed;
    const auto allowed = TransferValidation::recordStagingCleanup(state, target);
    if (!allowed) return allowed;
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, state);
    if (!binding) return Result<TransferAttempt>::failure(binding.error());
    const auto& movement = binding.value().movement;
    auto session = cleanup_.open(state, movement ? &*movement : nullptr);
    if (!session) return Result<TransferAttempt>::failure(session.error());
    const auto verified = session.value()->verify();
    if (!verified) return Result<TransferAttempt>::failure(verified.error());
    if (state.stagingCleanup == StagingCleanup::None) {
        const auto intent = journal_.recordStagingCleanup(state, binding.value().planHash, StagingCleanup::IntentRecorded);
        if (!intent) return Result<TransferAttempt>::failure(intent.error());
        state = allowed.value();
    }
    const auto removed = session.value()->removeVerifiedStaging();
    if (!removed) return Result<TransferAttempt>::failure(removed.error());
    const auto saved = journal_.recordStagingCleanup(state, binding.value().planHash, StagingCleanup::Removed);
    if (!saved) return Result<TransferAttempt>::failure(saved.error());
    return TransferValidation::recordStagingCleanup(state, StagingCleanup::Removed);
}
} // namespace nookbit
