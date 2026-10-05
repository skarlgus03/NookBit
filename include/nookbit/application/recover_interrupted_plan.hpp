#pragma once
#include <nookbit/domain/model.hpp>

namespace nookbit {
enum class RecoveryAction { Resume, Rollback, Later };

/// 사람이 선택한 작업과 표시 당시 승인 해시를 함께 전달한다.
struct RecoveryRequest {
    PlanId planId;
    std::string planHash;
    RecoveryAction action{RecoveryAction::Later};
};
struct RecoveryStepReset {
    StepId stepId;
    StepStatus expected;
    StepStatus restored;
};
/// 현재 상태 비교와 선택 감사·재시작 상태 복원을 한 트랜잭션에 요청한다.
struct RecoveryRestart {
    RecoveryRequest request;
    PlanStatus expectedStatus;
    std::vector<RecoveryStepReset> steps;
};
} // namespace nookbit
