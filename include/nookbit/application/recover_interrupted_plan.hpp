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
/// 삭제/완료 증거가 아니라 되돌리기 뒤 남겨둔 임시 파일의 추적 기록.
struct PreservedTransfer {
    std::string attemptId;
    StepId stepId;
    std::filesystem::path staging;
};
/// 부분 임시 파일의 명시적 정리 승인 지문과 결과를 이동 기록과 분리한다.
struct PreservedCleanupRecord {
    PlanId planId;
    std::string planHash;
    std::string attemptId;
    FileFingerprint stagingFingerprint;
    bool removed{false};
};
/// 현재 상태 비교와 선택 감사·재시작 상태 복원을 한 트랜잭션에 요청한다.
struct RecoveryRestart {
    RecoveryRequest request;
    PlanStatus expectedStatus;
    std::vector<RecoveryStepReset> steps;
    std::vector<PreservedTransfer> preserved;
};
} // namespace nookbit
