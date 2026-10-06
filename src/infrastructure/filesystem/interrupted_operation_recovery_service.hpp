#pragma once
#include <nookbit/application/recover_interrupted_plan_use_case.hpp>
#include <nookbit/application/interrupted_operations.hpp>

namespace nookbit {
/// 다른 파일 작업과 직렬화하고 명시적 전체 복구 선택만 전달한다.
class InterruptedOperationRecoveryService final : public IInterruptedOperations {
public:
    /// 외부에서 조립한 전체 복구 유스케이스를 빌려 받는 함수.
    explicit InterruptedOperationRecoveryService(RecoverInterruptedPlanUseCase& useCase) : useCase_(useCase) {}
    /// 앱 조회에 필요한 외부 소유 진단·이력·복구 기능을 받는 함수.
    InterruptedOperationRecoveryService(InspectInterruptedPlanUseCase& inspector, IHistoryRepository& history,
        RecoverInterruptedPlanUseCase& useCase) : useCase_(useCase), inspector_(&inspector), history_(&history) {}
    /// 공유 잠금 아래 실행과 같은 사전 검사를 적용한 표시 후보를 읽는 함수.
    [[nodiscard]] Result<std::vector<InterruptedOperationView>> inspectAll() override;
    /// 시스템 작업 잠금을 얻은 뒤 사용자 선택을 재검사하고 실행하는 함수.
    [[nodiscard]] Result<void> execute(const RecoveryRequest& request) override;
private:
    RecoverInterruptedPlanUseCase& useCase_;
    InspectInterruptedPlanUseCase* inspector_{nullptr};
    IHistoryRepository* history_{nullptr};
};
}
