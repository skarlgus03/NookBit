#pragma once
#include <nookbit/application/recover_interrupted_plan_use_case.hpp>

namespace nookbit {
/// 다른 파일 작업과 직렬화하고 명시적 전체 복구 선택만 전달한다.
class InterruptedOperationRecoveryService final {
public:
    /// 외부에서 조립한 전체 복구 유스케이스를 빌려 받는 함수.
    explicit InterruptedOperationRecoveryService(RecoverInterruptedPlanUseCase& useCase) : useCase_(useCase) {}
    /// 시스템 작업 잠금을 얻은 뒤 사용자 선택을 재검사하고 실행하는 함수.
    [[nodiscard]] Result<void> execute(const RecoveryRequest& request);
private:
    RecoverInterruptedPlanUseCase& useCase_;
};
}
