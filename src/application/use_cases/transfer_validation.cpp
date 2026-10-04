#include <nookbit/domain/transfer.hpp>

#include "application/use_cases/plan_signature.hpp"
#include <algorithm>
#include <cwctype>

namespace nookbit::TransferValidation {
namespace {

// 기록할 객체 지문에 볼륨 ID와 파일 ID가 있는지 검사하는 함수.
bool validObject(const FileFingerprint& fingerprint) {
    return fingerprint.volumeId && !fingerprint.volumeId->empty() &&
           fingerprint.fileId && !fingerprint.fileId->empty();
}

// 경로가 절대 경로이며 파일을 가리킬 수 있는지 검사하는 함수.
bool validPath(const std::filesystem::path& path) {
    const auto normalized = path.lexically_normal();
    return !path.empty() && path.is_absolute() && normalized.has_filename() &&
           normalized != normalized.root_path() && path.native().find(
               std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}

// Windows에서는 대소문자 차이도 같은 경로로 판단하는 함수.
bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
#ifdef _WIN32
    auto leftText = left.lexically_normal().wstring();
    auto rightText = right.lexically_normal().wstring();
    for (auto& character : leftText) character = static_cast<wchar_t>(std::towlower(character));
    for (auto& character : rightText) character = static_cast<wchar_t>(std::towlower(character));
    return leftText == rightText;
#else
    return left.lexically_normal() == right.lexically_normal();
#endif
}

} // namespace

// 복사본의 크기와 유효한 SHA-256 내용이 같은지 검사하는 함수.
bool sameContent(const FileFingerprint& left, const FileFingerprint& right) {
    return left.size == right.size && left.sha256 && right.sha256 &&
           PlanSignature::isSha256Hex(*left.sha256) &&
           PlanSignature::isSha256Hex(*right.sha256) && left.sha256 == right.sha256;
}

// 파일 객체의 볼륨 ID와 파일 ID가 같은지 검사하는 함수.
bool sameObject(const FileFingerprint& left, const FileFingerprint& right) {
    return validObject(left) && validObject(right) &&
           left.volumeId == right.volumeId && left.fileId == right.fileId;
}

// 오류·취소·완료로 종료한 시도인지 반환하는 함수.
bool isTerminal(TransferPhase phase) {
    return phase == TransferPhase::Completed || phase == TransferPhase::Failed ||
           phase == TransferPhase::Cancelled || phase == TransferPhase::NeedsRecovery;
}

// 순서 건너뛰기·완료 상태 재사용 없이 단계 전이가 가능한지 검사하는 함수.
bool canAdvance(TransferPhase from, TransferPhase to) {
    if (static_cast<int>(from) < 0 || from > TransferPhase::SourceRemoved) return false;
    if (isTerminal(from)) return false;
    if (to == TransferPhase::NeedsRecovery) return true;
    if (to == TransferPhase::Failed || to == TransferPhase::Cancelled) {
        return from == TransferPhase::Prepared || from == TransferPhase::CopyIntent ||
               from == TransferPhase::StagingCreated || from == TransferPhase::CopyVerified;
    }
    switch (from) {
    case TransferPhase::Prepared: return to == TransferPhase::CopyIntent;
    case TransferPhase::CopyIntent: return to == TransferPhase::StagingCreated;
    case TransferPhase::StagingCreated: return to == TransferPhase::CopyVerified;
    case TransferPhase::CopyVerified: return to == TransferPhase::CommitIntent;
    case TransferPhase::CommitIntent: return to == TransferPhase::DestinationCommitted;
    case TransferPhase::DestinationCommitted: return to == TransferPhase::SourceRemoveIntent;
    case TransferPhase::SourceRemoveIntent: return to == TransferPhase::SourceRemoved;
    case TransferPhase::SourceRemoved: return to == TransferPhase::Completed;
    default: return false;
    }
}

// 새 전송 시도의 경로와 원본 지문·초기 상태를 검사하는 함수.
Result<void> validateNew(const TransferAttempt& attempt) {
    if (attempt.id.empty() || attempt.planId.empty() || attempt.stepId.empty() ||
        (attempt.direction != TransferDirection::Forward && attempt.direction != TransferDirection::Rollback) ||
        attempt.phase != TransferPhase::Prepared || attempt.stagingFingerprint ||
        attempt.destinationFingerprint || attempt.error || attempt.recoveryCompleted || attempt.recoveryRemovalIntent || attempt.stagingCleanup != StagingCleanup::None ||
        !validPath(attempt.source) || !validPath(attempt.destination) || !validPath(attempt.staging) ||
        samePath(attempt.source, attempt.destination) ||
        samePath(attempt.staging, attempt.destination) ||
        samePath(attempt.staging, attempt.source) ||
        !samePath(attempt.staging.parent_path(), attempt.destination.parent_path()) ||
        !validObject(attempt.sourceFingerprint) || !sameContent(attempt.sourceFingerprint, attempt.sourceFingerprint)) {
        return Result<void>::failure({"transfer.invalid_attempt", "A prepared attempt needs distinct absolute paths and a hashed source."});
    }
    return Result<void>::success();
}

// 허용된 증거만 합쳐 다음 단계의 스냅샷을 만드는 함수.
Result<TransferAttempt> advance(const TransferAttempt& attempt, const TransferUpdate& update) {
    if (attempt.recoveryRemovalIntent || attempt.stagingCleanup != StagingCleanup::None || attempt.phase != update.expectedPhase || !canAdvance(attempt.phase, update.nextPhase)) {
        return Result<TransferAttempt>::failure({"transfer.phase_conflict", "Transfer phase changed or the transition is not allowed."});
    }
    if (attempt.phase >= TransferPhase::CopyVerified && attempt.phase <= TransferPhase::SourceRemoved &&
        (!attempt.stagingFingerprint || !validObject(*attempt.stagingFingerprint) ||
         !sameContent(attempt.sourceFingerprint, *attempt.stagingFingerprint))) {
        return Result<TransferAttempt>::failure({"transfer.invalid_evidence", "A verified copy needs previously recorded staging evidence."});
    }
    if (attempt.phase >= TransferPhase::DestinationCommitted && attempt.phase <= TransferPhase::SourceRemoved &&
        (!attempt.destinationFingerprint || !sameObject(*attempt.stagingFingerprint, *attempt.destinationFingerprint) ||
         !sameContent(attempt.sourceFingerprint, *attempt.destinationFingerprint))) {
        return Result<TransferAttempt>::failure({"transfer.invalid_evidence", "Source removal needs a previously verified committed destination."});
    }
    const bool stagingCreated = update.nextPhase == TransferPhase::StagingCreated;
    const bool copyVerified = update.nextPhase == TransferPhase::CopyVerified;
    const bool destinationCommitted = update.nextPhase == TransferPhase::DestinationCommitted;
    const bool failure = update.nextPhase == TransferPhase::Failed || update.nextPhase == TransferPhase::NeedsRecovery;
    if (update.stagingFingerprint.has_value() != (stagingCreated || copyVerified) ||
        update.destinationFingerprint.has_value() != destinationCommitted ||
        update.error.has_value() != failure || (update.error && update.error->code.empty())) {
        return Result<TransferAttempt>::failure({"transfer.invalid_evidence", "Evidence must match the recorded phase."});
    }
    if (stagingCreated && (!validObject(*update.stagingFingerprint) ||
        sameObject(attempt.sourceFingerprint, *update.stagingFingerprint) ||
        (update.stagingFingerprint->sha256 && !PlanSignature::isSha256Hex(*update.stagingFingerprint->sha256)))) {
        return Result<TransferAttempt>::failure({"transfer.invalid_evidence", "Staging creation needs an object identity."});
    }
    if (copyVerified && (!attempt.stagingFingerprint ||
        !sameObject(*attempt.stagingFingerprint, *update.stagingFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *update.stagingFingerprint))) {
        return Result<TransferAttempt>::failure({"transfer.copy_mismatch", "The verified copy must keep its staging identity and approved content."});
    }
    if (destinationCommitted && (!attempt.stagingFingerprint ||
        !sameObject(*attempt.stagingFingerprint, *update.destinationFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *update.destinationFingerprint))) {
        return Result<TransferAttempt>::failure({"transfer.destination_mismatch", "The committed destination must be the verified staging object."});
    }
    auto next = attempt;
    next.phase = update.nextPhase;
    if (update.stagingFingerprint) next.stagingFingerprint = update.stagingFingerprint;
    if (update.destinationFingerprint) next.destinationFingerprint = update.destinationFingerprint;
    next.error = update.error;
    return Result<TransferAttempt>::success(std::move(next));
}

// 일반 단계 전이를 열어두지 않고 명시적 복구 완료만 검증하는 함수.
Result<TransferAttempt> reconcileCompleted(const TransferAttempt& attempt, bool removalIntentRecorded) {
    if (!removalIntentRecorded || attempt.recoveryCompleted || attempt.stagingCleanup != StagingCleanup::None || (attempt.phase != TransferPhase::SourceRemoveIntent &&
        attempt.phase != TransferPhase::SourceRemoved && attempt.phase != TransferPhase::NeedsRecovery &&
        !(attempt.phase == TransferPhase::DestinationCommitted && attempt.recoveryRemovalIntent)) ||
        !attempt.stagingFingerprint || !attempt.destinationFingerprint ||
        !sameObject(*attempt.stagingFingerprint, *attempt.destinationFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *attempt.destinationFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *attempt.stagingFingerprint))
        return Result<TransferAttempt>::failure({"transfer.recovery_state", "Recorded removal intent and verified final evidence are required"});
    auto result = attempt;
    result.phase = TransferPhase::Completed;
    result.error.reset();
    result.recoveryCompleted = true;
    return Result<TransferAttempt>::success(std::move(result));
}

// 파일 정리가 이동 완료나 재실행 허가로 해석되지 않도록 별도 감사값만 갱신하는 함수.
Result<TransferAttempt> recordStagingCleanup(const TransferAttempt& attempt, StagingCleanup next) {
    if ((attempt.phase != TransferPhase::Failed && attempt.phase != TransferPhase::Cancelled && attempt.phase != TransferPhase::NeedsRecovery) ||
        attempt.recoveryCompleted || attempt.recoveryRemovalIntent || attempt.destinationFingerprint || !attempt.stagingFingerprint ||
        !validObject(*attempt.stagingFingerprint) || sameObject(attempt.sourceFingerprint, *attempt.stagingFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *attempt.stagingFingerprint) ||
        !((attempt.stagingCleanup == StagingCleanup::None && next == StagingCleanup::IntentRecorded) ||
          (attempt.stagingCleanup == StagingCleanup::IntentRecorded && next == StagingCleanup::Removed)))
        return Result<TransferAttempt>::failure({"transfer.cleanup_state", "Only a stopped, recorded verified staging copy can be cleaned"});
    auto result = attempt;
    result.stagingCleanup = next;
    return Result<TransferAttempt>::success(std::move(result));
}

// 일반 단계 재개를 허용하지 않고 기록된 최종 복사본의 제거 의도만 만드는 함수.
Result<TransferAttempt> recordRecoveryRemovalIntent(const TransferAttempt& attempt) {
    if (attempt.recoveryRemovalIntent || attempt.recoveryCompleted || attempt.stagingCleanup != StagingCleanup::None ||
        (attempt.phase != TransferPhase::DestinationCommitted && attempt.phase != TransferPhase::SourceRemoveIntent &&
         attempt.phase != TransferPhase::NeedsRecovery) || !attempt.stagingFingerprint || !attempt.destinationFingerprint ||
        !sameObject(*attempt.stagingFingerprint, *attempt.destinationFingerprint) ||
        sameObject(attempt.sourceFingerprint, *attempt.destinationFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *attempt.stagingFingerprint) ||
        !sameContent(attempt.sourceFingerprint, *attempt.destinationFingerprint))
        return Result<TransferAttempt>::failure({"transfer.resume_state", "Stopped committed copy evidence is required"});
    auto result = attempt;
    result.recoveryRemovalIntent = true;
    return Result<TransferAttempt>::success(std::move(result));
}
} // namespace nookbit::TransferValidation
