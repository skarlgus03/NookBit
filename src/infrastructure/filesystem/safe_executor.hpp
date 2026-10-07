#pragma once

#include <nookbit/application/ports.hpp>
#include <nookbit/application/staged_copy.hpp>
#include <nookbit/application/execute_transfer_step_use_case.hpp>
#include <nookbit/application/rollback_transfer_use_case.hpp>
#include <nookbit/application/recover_interrupted_plan_use_case.hpp>
#include <nookbit/application/use_cases/verify_saved_plan.hpp>

#include <filesystem>

namespace nookbit {

/// 승인된 계획의 파일들을 순서대로 이동하고 안전하게 되돌리는 실행기.
class SafeExecutor final : public IExecutor, public IRecoveredPlanRunner {
public:
    /// 현재 상태 검증과 이력 저장에 필요한 기능을 받는 생성자.
    SafeExecutor(ISettingsRepository& settings,
                 IVerifySavedPlanUseCase& verifier,
                 IScanner& scanner,
                 IPlanHasher& hasher,
                 IHistoryRepository& history,
                 std::filesystem::path appDataRoot,
                 ITransferVolumeInspector* volumes = nullptr,
                 ITransferStepExecutor* transfers = nullptr,
                 IRollbackTransferStepExecutor* reversals = nullptr);

    /// 검증이 필요 없는 되돌리기만 할 때 사용할 실행기를 만드는 생성자.
    SafeExecutor(ISettingsRepository& settings,
                 IScanner& scanner,
                 IPlanHasher& hasher,
                 IHistoryRepository& history,
                 std::filesystem::path appDataRoot,
                 ITransferVolumeInspector* volumes = nullptr,
                 IRollbackTransferStepExecutor* reversals = nullptr);

    /// 승인된 계획의 파일들을 덮어쓰기 없이 순서대로 실행하는 함수.
    [[nodiscard]] Result<void> execute(
        const OperationPlan& plan,
        const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selectedFiles) override;

    /// 완료된 계획의 이동 파일들을 역순으로 되돌리는 함수.
    [[nodiscard]] Result<void> rollback(const PlanId& planId) override;

    /// 복구 선택 기록 전에 서비스 구성과 모든 경로의 안전 범위를 확인하는 함수.
    [[nodiscard]] Result<void> verifyRecoverySupport(const OperationPlan& plan, RecoveryAction action) override;
    /// 명시적 선택으로 원자적 복원된 계획을 기존 이동·되돌리기 흐름으로 실행하는 함수.
    [[nodiscard]] Result<void> runRecoveredPlan(const OperationPlan& plan, RecoveryAction action) override;

private:
    /// 빠른 같은 볼륨 이동을 개별 시도하고 확실한 실패만 기록하며 다음 파일을 처리하는 함수.
    [[nodiscard]] Result<void> executeMetadataSteps(const OperationPlan& plan,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings);
    /// 최초 실행과 복구 실행이 같은 단계 이동·보상·완료 기록을 공유하는 함수.
    [[nodiscard]] Result<void> executePreparedSteps(const OperationPlan& plan,
        const std::vector<const MoveStep*>& steps, std::vector<const MoveStep*> appliedSteps,
        bool sourceContentAtCopyOpen = false);
    /// v2의 승인된 볼륨과 이동 전략을 실제 실행 직전에 재검사하는 함수.
    [[nodiscard]] Result<void> verifyMovement(const OperationPlan& plan, const MoveStep* step = nullptr);
    /// 이미 적용된 단계들을 검증하고 역순으로 되돌려 최종 상태를 기록하는 함수.
    [[nodiscard]] Result<void> rollbackAppliedSteps(
        const OperationPlan& plan,
        const std::vector<const MoveStep*>& applied,
        PlanStatus finalStatus,
        bool duringExecution);

    /// 확인 불가 상태를 기록하고 작업을 중단하는 함수.
    [[nodiscard]] Result<void> markNeedsRecovery(const PlanId& planId);

    ISettingsRepository& settings_;
    IVerifySavedPlanUseCase* verifier_;
    IScanner& scanner_;
    IPlanHasher& hasher_;
    IHistoryRepository& history_;
    std::filesystem::path appDataRoot_;
    ITransferVolumeInspector* volumes_;
    ITransferStepExecutor* transfers_{nullptr};
    IRollbackTransferStepExecutor* reversals_{nullptr};
};

} // namespace nookbit
