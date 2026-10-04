#include <nookbit/application/commit_transfer_copy_use_case.hpp>

namespace nookbit {

// 외부에서 만든 복사 일꾼과 기록 포트를 빌려 받는 함수.
CommitTransferCopyUseCase::CommitTransferCopyUseCase(IStagedCopy& copy, ITransferJournal& journal)
    : copy_(copy), journal_(journal) {}

// 기록 경계가 성공한 경우에만 다음 파일 작업을 진행하는 함수.
Result<TransferAttempt> CommitTransferCopyUseCase::execute(
    const TransferAttempt& attempt, const std::function<bool()>& cancelled, const MoveContract* movement) {
    const auto valid = TransferValidation::validateNew(attempt);
    if (!valid) return Result<TransferAttempt>::failure(valid.error());
    const auto begun = journal_.begin(attempt);
    if (!begun) return Result<TransferAttempt>::failure(begun.error());
    auto state = attempt;

    // 순수 계약과 저장이 모두 성공해야 로컬 단계도 갱신하는 함수.
    const auto record = [&](TransferUpdate update) -> Result<void> {
        const auto next = TransferValidation::advance(state, update);
        if (!next) return Result<void>::failure(next.error());
        const auto saved = journal_.advance(state.id, update);
        if (!saved) return saved;
        state = next.value();
        return Result<void>::success();
    };
    // 결과가 불확실하면 재시도를 막는 복구 필요 기록을 시도하는 함수.
    const auto stop = [&](Error error, TransferPhase phase) -> Result<TransferAttempt> {
        TransferUpdate update;
        update.expectedPhase = state.phase;
        update.nextPhase = phase;
        if (phase != TransferPhase::Cancelled) update.error = error;
        const auto saved = record(update);
        if (!saved) {
            error.message += "; recovery/status record failed: " + saved.error().code;
            error.code = "transfer.record_failed";
        }
        return Result<TransferAttempt>::failure(std::move(error));
    };
    // 파일 작업 전 저장 실패를 복구 필요 상태로 전달하는 함수.
    const auto recordFailure = [&](const Error& error) {
        return stop(error, TransferPhase::NeedsRecovery);
    };
    // 확정 의도 기록 전까지만 취소를 허용하는 함수.
    const auto wantsCancel = [&] { return cancelled && cancelled(); };
    if (wantsCancel()) return stop({"transfer.cancelled", "Cancelled before copying"}, TransferPhase::Cancelled);

    auto saved = record({state.phase, TransferPhase::CopyIntent});
    if (!saved) return recordFailure(saved.error());
    auto session = movement ? copy_.openApproved(state.source, state.sourceFingerprint, state.staging, *movement) :
        copy_.open(state.source, state.sourceFingerprint, state.staging);
    // 생성 뒤 지문 조회 실패도 가능하므로 파일명만 보고 실패/재시도 가능으로 판단하지 않는다.
    if (!session) return stop(session.error(), TransferPhase::NeedsRecovery);
    saved = record({state.phase, TransferPhase::StagingCreated, session.value()->createdFingerprint()});
    if (!saved) return recordFailure(saved.error());
    const auto verified = session.value()->copyAndVerify(cancelled);
    if (!verified) return stop(verified.error(), verified.error().code == "transfer.cancelled"
        ? TransferPhase::Cancelled : TransferPhase::Failed);
    saved = record({state.phase, TransferPhase::CopyVerified, verified.value()});
    if (!saved) return recordFailure(saved.error());
    if (wantsCancel()) return stop({"transfer.cancelled", "Cancelled before commit"}, TransferPhase::Cancelled);
    saved = record({state.phase, TransferPhase::CommitIntent});
    if (!saved) return recordFailure(saved.error());
    // 이 지점부터 취소나 파일 오류로 실제 확정 여부를 숨기지 않는다.
    const auto committed = session.value()->commitNoReplace(state.destination);
    if (!committed) return stop(committed.error(), TransferPhase::NeedsRecovery);
    saved = record({state.phase, TransferPhase::DestinationCommitted, {}, committed.value()});
    if (!saved) return recordFailure(saved.error());
    return Result<TransferAttempt>::success(std::move(state));
}

} // namespace nookbit
