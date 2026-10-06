#pragma once
#include <nookbit/application/reconcile_transfer_recovery_use_case.hpp>
#include <nookbit/application/cleanup_transfer_staging_use_case.hpp>
#include <nookbit/application/cleanup_preserved_staging_use_case.hpp>
#include <nookbit/application/resume_committed_transfer_use_case.hpp>
#include <nookbit/application/resume_partial_copy_use_case.hpp>
#include <nookbit/application/commit_resumed_staging_use_case.hpp>
#include <nookbit/application/reconcile_transfer_commit_use_case.hpp>

namespace nookbit {
/// 기록 누락 확정의 원본·최종 파일을 읽기 전용으로 보호하는 어댑터.
class WindowsTransferCommitRecoveryGuard final : public ITransferCommitRecoveryGuard {
public:
    /// 파일 변경 없이 승인 볼륨과 두 객체를 보호하는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferCommitRecoverySession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) override;
};
/// 다른 실행·복구와 같은 시스템 잠금 안에서 확정 기록만 복구하는 진입점.
class TransferCommitRecoveryService final {
public:
    /// 외부 소유의 기록 복구 유스케이스를 빌려 받는 함수.
    explicit TransferCommitRecoveryService(ReconcileTransferCommitUseCase& useCase) : useCase_(useCase) {}
    /// 파일을 변경하지 않고 누락 확정 결과의 명시적 복구를 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    ReconcileTransferCommitUseCase& useCase_;
};
/// 다른 실행·복구와 직렬화해 재개 복사본의 최종 이름을 확정하는 진입점.
class ResumedStagingCommitService final {
public:
    /// 외부 소유의 확정 유스케이스를 빌려 받는 함수.
    explicit ResumedStagingCommitService(CommitResumedStagingUseCase& useCase) : useCase_(useCase) {}
    /// 같은 시스템 잠금 안에서 명시적 최종 확정을 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    CommitResumedStagingUseCase& useCase_;
};
/// 기존 실행·복구와 직렬화해 부분 임시 복사본만 이어 쓰는 진입점.
class PartialCopyResumeService final {
public:
    /// 외부 소유의 이어쓰기 유스케이스를 빌려 받는 함수.
    explicit PartialCopyResumeService(ResumePartialCopyUseCase& useCase) : useCase_(useCase) {}
    /// 같은 시스템 잠금 안에서 명시적 이어쓰기를 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    ResumePartialCopyUseCase& useCase_;
};
/// 실행·복구와 같은 시스템 잠금 안에서 중단된 확정 전송을 마무리하는 진입점.
class CommittedTransferResumeService final {
public:
    /// 외부 소유의 전송 마무리 유스케이스를 빌려 받는 함수.
    explicit CommittedTransferResumeService(ResumeCommittedTransferUseCase& useCase) : useCase_(useCase) {}
    /// 다른 실행과 직렬화한 뒤 명시적 전송 마무리를 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    ResumeCommittedTransferUseCase& useCase_;
};
/// 기록 확정 동안 최종 파일과 부모를 보호하며 파일 변경은 제공하지 않는 어댑터.
class WindowsTransferRecoveryGuard final : public ITransferRecoveryGuard {
public:
    /// 원본·임시 파일 부재와 최종 복사본을 읽기 전용으로 보호하는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferRecoverySession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) override;
};

/// 경로 삭제가 아닌 검증된 임시 복사본 핸들 제거만 제공하는 어댑터.
class WindowsStagingCleanup final : public IStagingCleanup {
public:
    /// 원본과 임시 객체 및 승인 볼륨을 보호하며 정리 세션을 여는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagingCleanupSession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) override;
};

class StagingCleanupService final {
public:
    /// 외부 소유 정리 유스케이스를 빌려 받는 함수.
    explicit StagingCleanupService(CleanupTransferStagingUseCase& useCase) : useCase_(useCase) {}
    /// 실행·복구와 공유하는 시스템 잠금 안에서 명시적 임시 정리를 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId);
private:
    CleanupTransferStagingUseCase& useCase_;
};
/// 전체 검증 복사본 정리와 별도로 보존된 부분 파일만 보호하는 어댑터.
class WindowsPreservedStagingCleanup final : public IPreservedStagingCleanup {
public:
    /// 기록된 객체와 승인 지문을 보호하고 같은 임시 객체의 삭제만 제공하는 함수.
    [[nodiscard]] Result<std::unique_ptr<IStagingCleanupSession>> openPreserved(const TransferAttempt& attempt,
        const MoveContract& movement, const FileFingerprint& expected, bool priorIntent) override;
};
class PreservedStagingCleanupService final {
public:
    /// 외부에서 조립한 부분 파일 정리 유스케이스를 받는 함수.
    explicit PreservedStagingCleanupService(CleanupPreservedStagingUseCase& useCase) : useCase_(useCase) {}
    /// 공유 작업 잠금 아래 승인에 사용할 읽기 전용 검사를 수행하는 함수.
    [[nodiscard]] Result<PreservedCleanupRecord> inspect(const PlanId& planId, const std::string& attemptId);
    /// 다른 실행·복구와 직렬화한 뒤 명시적 정리 승인을 전달하는 함수.
    [[nodiscard]] Result<void> execute(const PreservedCleanupRequest& request);
private:
    CleanupPreservedStagingUseCase& useCase_;
};

/// 실행·되돌리기와 같은 시스템 잠금 안에서 명시적 완료 복구를 수행하는 진입점.
class TransferRecoveryService final {
public:
    /// 외부 소유의 복구 유스케이스를 빌려 받는 함수.
    explicit TransferRecoveryService(ReconcileTransferRecoveryUseCase& useCase) : useCase_(useCase) {}
    /// 다른 앱 인스턴스와 작업을 직렬화한 뒤 완료 기록 복구를 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId);
private:
    ReconcileTransferRecoveryUseCase& useCase_;
};
} // namespace nookbit
