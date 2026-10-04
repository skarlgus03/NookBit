#include <nookbit/application/resume_partial_copy_use_case.hpp>
#include "transfer_recovery_binding.hpp"

namespace nookbit {
// 외부에서 생성한 기록·복사·서명 검사 일꾼을 빌려 받는 함수.
ResumePartialCopyUseCase::ResumePartialCopyUseCase(const IHistoryRepository& history, ITransferJournal& journal,
    IStagedCopy& copy, const IPlanHasher& hasher) : history_(history), journal_(journal), copy_(copy), hasher_(hasher) {}

// 같은 보호 세션에서 의도 저장 후 이어 쓰고 전체 검증 결과만 기록하는 함수.
Result<TransferAttempt> ResumePartialCopyUseCase::execute(const std::string& attemptId, const std::function<bool()>& cancelled) {
    using Return = Result<TransferAttempt>;
    const auto loaded = journal_.load(attemptId);
    if (!loaded) return Return::failure(loaded.error());
    auto state = loaded.value();
    auto eligible = state;
    if (eligible.copyResume == CopyResume::IntentRecorded) eligible.copyResume = CopyResume::None;
    const auto intent = TransferValidation::recordCopyResume(eligible, CopyResume::IntentRecorded);
    if (!intent) return intent;
    const auto binding = verifyTransferRecoveryBinding(history_, journal_, hasher_, state);
    if (!binding) return Return::failure(binding.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Resume cancelled before any writes"});
    const auto& movement = binding.value().movement;
    auto session = copy_.openResume(state, movement ? &*movement : nullptr, cancelled);
    if (!session) return Return::failure(session.error());
    if (cancelled && cancelled()) return Return::failure({"transfer.cancelled", "Resume cancelled before intent"});
    const auto savedIntent = journal_.recordCopyResume(state, binding.value().planHash, CopyResume::IntentRecorded);
    if (!savedIntent) return Return::failure(savedIntent.error());
    state = intent.value();
    const auto verified = session.value()->copyAndVerify(cancelled);
    if (!verified) return Return::failure(verified.error());
    const auto saved = journal_.recordCopyResume(state, binding.value().planHash, CopyResume::Verified, verified.value());
    if (!saved) return Return::failure(saved.error());
    return TransferValidation::recordCopyResume(state, CopyResume::Verified, verified.value());
}
}
