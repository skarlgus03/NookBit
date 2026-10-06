#pragma once
#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>

namespace nookbit {
/// 표시 시점의 진단·승인 스냅샷과 선택 차단 이유를 전달하는 값.
struct InterruptedOperationView {
    InterruptedPlanInspection inspection;
    std::string planHash;
    int formatVersion{1};
    bool canResume{false}, canRollback{false};
    std::optional<Error> resumeBlock, rollbackBlock;
};
/// 화면에 파일 변경·실행기 소유 책임을 주지 않는 전체 복구 계약.
class IInterruptedOperations {
public:
    /// 복구 계약 구현을 안전하게 해제하는 함수.
    virtual ~IInterruptedOperations() = default;
    /// 중단 작업과 보존된 부분 파일을 실행 없이 조회하는 함수.
    [[nodiscard]] virtual Result<std::vector<InterruptedOperationView>> inspectAll() = 0;
    /// 표시 때의 승인 해시와 사용자 명시적 선택을 재검사해 전달하는 함수.
    [[nodiscard]] virtual Result<void> execute(const RecoveryRequest& request) = 0;
};
}
