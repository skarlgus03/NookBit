#pragma once
#include <nookbit/application/recover_interrupted_plan_use_case.hpp>
#include <nookbit/application/reconcile_transfer_commit_use_case.hpp>
#include <nookbit/application/resume_committed_transfer_use_case.hpp>
#include <nookbit/application/resume_partial_copy_use_case.hpp>
#include <nookbit/application/commit_resumed_staging_use_case.hpp>

namespace nookbit {
/// 파일 일꾼을 소유하지 않고 기존 개별 복구 유스케이스를 정방향 순서로 조율한다.
class ResumeInterruptedTransferUseCase final : public IInterruptedTransferResolver {
public:
    /// 외부 소유의 기록·복사·제거·보호 검사·해시 포트를 빌려 받는 함수.
    ResumeInterruptedTransferUseCase(const IHistoryRepository& history, ITransferJournal& journal,
        IStagedCopy& copy, ITransferSourceRemoval& removal, ITransferFileInspector& files,
        ITransferRecoveryGuard& removalGuard, ITransferCommitRecoveryGuard& commitGuard, const IPlanHasher& hasher);
    /// 지원하는 정방향 경계와 승인 연결을 변경 없이 검사하는 함수.
    [[nodiscard]] Result<void> verifyResume(const TransferRecoveryInspection& inspection) override;
    /// 공유 잠금 아래 새 진단마다 기존 보호 절차로 전송을 완료하는 함수.
    [[nodiscard]] Result<void> resume(const std::string& attemptId) override;
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    IStagedCopy& copy_;
    ITransferSourceRemoval& removal_;
    ITransferFileInspector& files_;
    ITransferRecoveryGuard& removalGuard_;
    ITransferCommitRecoveryGuard& commitGuard_;
    const IPlanHasher& hasher_;
};
}
