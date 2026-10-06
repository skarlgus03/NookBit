#pragma once
#include <nookbit/application/file_operations.hpp>
#include <nookbit/application/execute_transfer_step_use_case.hpp>
#include <nookbit/application/rollback_transfer_use_case.hpp>
#include <nookbit/application/use_cases/verify_saved_plan.hpp>

namespace nookbit {
/// 앱 조립부가 소유한 포트로 요청별 실행기를 조립하며 UI에는 계약만 제공한다.
class FileOperationService final : public IFileOperations {
public:
    /// 화면과 독립된 조회·검증·기록·볼륨·전송 기능을 빌려 받는 함수.
    FileOperationService(IVerifySavedPlanUseCase& verifier, IScanner& scanner, IPlanHasher& hasher,
        IHistoryRepository& history, std::filesystem::path appDataRoot, ITransferVolumeInspector* volumes = nullptr,
        ITransferStepExecutor* forward = nullptr, IRollbackTransferStepExecutor* reverse = nullptr);
    /// 현재 설정을 요청 수명 안에 고정하고 기존 안전 실행기로 승인 작업을 전달하는 함수.
    [[nodiscard]] Result<void> executeApproved(const OperationPlan& plan, const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) override;
    /// 화면의 기본 목적지와 관계없이 저장된 이동 계약으로 되돌리는 함수.
    [[nodiscard]] Result<void> rollback(const PlanId& planId) override;
private:
    IVerifySavedPlanUseCase& verifier_;
    IScanner& scanner_;
    IPlanHasher& hasher_;
    IHistoryRepository& history_;
    std::filesystem::path appDataRoot_;
    ITransferVolumeInspector* volumes_;
    ITransferStepExecutor* forward_;
    IRollbackTransferStepExecutor* reverse_;
};
}
