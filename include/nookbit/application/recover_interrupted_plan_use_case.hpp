#pragma once
#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>

namespace nookbit {
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
        IRecoveredPlanRunner& runner);
    /// 호출자가 공유 잠금을 보유한 상태에서 사용자 선택을 실행하는 함수.
    [[nodiscard]] Result<void> execute(const RecoveryRequest& request);
private:
    InspectInterruptedPlanUseCase& inspector_;
    IHistoryRepository& history_;
    const ISettingsRepository& settings_;
    const IRuleSetRepository& rules_;
    const IPlanHasher& hasher_;
    IRecoveredPlanRunner& runner_;
};
} // namespace nookbit
