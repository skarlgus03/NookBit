#pragma once
#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>

namespace nookbit {
/// 명시적 정방향 재개에서만 미해결 전송을 기존 안전 절차로 끝내는 포트.
class IInterruptedTransferResolver {
public:
    /// 외부 소유 구현체를 인터페이스로 정리하는 함수.
    virtual ~IInterruptedTransferResolver() = default;
    /// 기록과 진단이 지원하는 복구 경계인지 변경 없이 검사하는 함수.
    [[nodiscard]] virtual Result<void> verifyResume(const TransferRecoveryInspection& inspection) = 0;
    /// 호출자가 공유 잠금을 보유한 상태에서 현재 기록을 다시 읽어 전송 하나를 마무리하는 함수.
    [[nodiscard]] virtual Result<void> resume(const std::string& attemptId) = 0;
};

/// 이미 선택·검사·기록된 전체 작업만 실행하는 포트.
class IRecoveredPlanRunner {
public:
    /// 실행 구현체를 인터페이스로 정리하는 함수.
    virtual ~IRecoveredPlanRunner() = default;
    /// 복구 기록 전에 필요한 실행 포트와 보호 경로가 준비됐는지 읽기 전용으로 검사하는 함수.
    [[nodiscard]] virtual Result<void> verifyRecoverySupport(const OperationPlan& plan, RecoveryAction action) = 0;
    /// 호출자가 공유 작업 잠금을 보유한 상태에서 남은 이동 또는 역순 복원을 수행하는 함수.
    [[nodiscard]] virtual Result<void> runRecoveredPlan(const OperationPlan& plan, RecoveryAction action) = 0;
};

/// 조회 결과를 승인으로 쓰지 않고 명시적 선택 직후 전체 작업을 다시 검사한다.
class RecoverInterruptedPlanUseCase final {
public:
    /// 외부에서 소유한 조회·설정·규칙·기록·실행 기능을 빌려 받는 함수.
    RecoverInterruptedPlanUseCase(InspectInterruptedPlanUseCase& inspector, IHistoryRepository& history,
        const ISettingsRepository& settings, const IRuleSetRepository& rules, const IPlanHasher& hasher,
        IRecoveredPlanRunner& runner, IInterruptedTransferResolver* transfers = nullptr);
    /// 호출자가 공유 잠금을 보유한 상태에서 사용자 선택을 실행하는 함수.
    [[nodiscard]] Result<void> execute(const RecoveryRequest& request);
private:
    /// 원래 승인과 현재 설정·규칙의 일치 여부를 다시 검사하는 함수.
    [[nodiscard]] Result<void> verifyConfiguration(const OperationPlan& plan) const;
    /// 선택 이후에도 동일한 승인 내용과 단계 식별자를 사용하는지 검사하는 함수.
    [[nodiscard]] Result<void> verifyChosenApproval(const OperationPlan& plan) const;
    InspectInterruptedPlanUseCase& inspector_;
    IHistoryRepository& history_;
    const ISettingsRepository& settings_;
    const IRuleSetRepository& rules_;
    const IPlanHasher& hasher_;
    IRecoveredPlanRunner& runner_;
    IInterruptedTransferResolver* transfers_;
};
} // namespace nookbit
