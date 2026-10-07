#pragma once

#include <nookbit/domain/transfer.hpp>

namespace nookbit {

/// 파일을 변경하지 않고 전송 시도와 단계별 증거만 기록하는 계약.
class ITransferJournal {
public:
    /// 구현체를 안전하게 정리하는 소멸자.
    virtual ~ITransferJournal() = default;
    /// 보호 세션의 제거 증거와 감사를 원자적으로 저장하며 다른 증거로 덮어쓰지 않는 함수.
    [[nodiscard]] virtual Result<void> recordRemovalProof(const TransferAttempt&, const TransferRemovalProof&) {
        return Result<void>::failure({"transfer.removal_proof_unsupported", "Durable removal proof is required."});
    }

    /// 기존 실행 단계에 연결된 새 전송 시도를 기록하는 함수.
    [[nodiscard]] virtual Result<void> begin(const TransferAttempt& attempt) = 0;

    /// 예상한 이전 단계가 같을 때만 증거와 다음 단계를 원자적으로 기록하는 함수.
    [[nodiscard]] virtual Result<void> advance(const std::string& attemptId, const TransferUpdate& update) = 0;
    /// 제거 결과가 기록된 정방향 시도·단계·계획 완료를 원자적으로 확정하는 함수.
    [[nodiscard]] virtual Result<void> completeForward(const std::string&) {
        return Result<void>::failure({"transfer.completion_unsupported", "Atomic forward completion is unsupported"});
    }
    /// 역전송·되돌린 단계·필요한 계획 완료를 원자적으로 확정하는 함수.
    [[nodiscard]] virtual Result<void> completeRollback(const std::string&) {
        return Result<void>::failure({"transfer.completion_unsupported", "Atomic rollback completion is unsupported"});
    }

    /// 보호 검사 중인 예상 시도·승인 해시가 그대로일 때 완료와 상위 기록을 원자적으로 복구하는 함수.
    [[nodiscard]] virtual Result<void> reconcileCompleted(const TransferAttempt&, const std::string&) {
        return Result<void>::failure({"transfer.recovery_unsupported", "Atomic recovery completion is unsupported"});
    }
    /// 예상 시도·승인 해시를 재확인하고 임시 정리 의도/결과와 감사 이력을 원자적으로 저장하는 함수.
    [[nodiscard]] virtual Result<void> recordStagingCleanup(const TransferAttempt&, const std::string&, StagingCleanup) {
        return Result<void>::failure({"transfer.cleanup_unsupported", "Durable staging cleanup is unsupported"});
    }

    /// 보호된 예상 시도·현재 승인을 확인하고 복구 제거 의도 또는 재시도 감사를 저장하는 함수.
    [[nodiscard]] virtual Result<void> recordRecoveryRemovalIntent(const TransferAttempt&, const std::string&) {
        return Result<void>::failure({"transfer.resume_unsupported", "Durable recovery removal is unsupported"});
    }

    /// 재시작 후 전송 시도의 마지막 스냅샷을 복원하는 함수.
    [[nodiscard]] virtual Result<TransferAttempt> load(const std::string& attemptId) const = 0;
    /// 예상 시도·현재 승인을 대조해 이어쓰기 의도/재시도/전체 검증 증거를 저장하는 함수.
    [[nodiscard]] virtual Result<void> recordCopyResume(const TransferAttempt&, const std::string&, CopyResume,
        const std::optional<FileFingerprint>& = {}) {
        return Result<void>::failure({"transfer.resume_unsupported", "Durable partial copy resume unsupported"});
    }

    /// 보호된 예상 시도와 현재 승인을 대조해 재개 복사본 확정 감사를 저장하는 함수.
    [[nodiscard]] virtual Result<void> recordStagingCommit(const TransferAttempt&, const std::string&, StagingCommit,
        const std::optional<FileFingerprint>& = {}) {
        return Result<void>::failure({"transfer.commit_resume_unsupported", "Durable staging commit unsupported"});
    }

    /// 보호 검증 중인 예상 시도·현재 승인·실제 확정 의도를 대조해 누락 결과만 저장하는 함수.
    [[nodiscard]] virtual Result<void> reconcileDestinationCommitted(const TransferAttempt&, const std::string&,
        const FileFingerprint&) {
        return Result<void>::failure({"transfer.commit_recovery_unsupported", "Durable commit recovery unsupported"});
    }
    /// 단계 하나의 정방향·역방향 시도를 생성 순서대로 읽는 함수.
    [[nodiscard]] virtual Result<std::vector<TransferAttempt>> forStep(const StepId& stepId) const = 0;
};

} // namespace nookbit
