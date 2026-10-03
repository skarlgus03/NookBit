#pragma once

#include <nookbit/application/staged_copy.hpp>

namespace nookbit {

/// 파일 변경 없이 초안에 볼륨·이동 계약을 붙이고 기존 계약을 재확인한다.
class PrepareTransferPlanUseCase final {
public:
    /// 외부에서 소유한 읽기 전용 볼륨 조회 기능을 빌려 받는 함수.
    explicit PrepareTransferPlanUseCase(ITransferVolumeInspector& volumes);
    /// 서명되지 않은 초안의 실제 볼륨을 조회해 v2 계획으로 만드는 함수.
    [[nodiscard]] Result<OperationPlan> execute(const OperationPlan& draft);
    /// 현재 볼륨이 승인 계약과 동일한지 파일 변경 없이 확인하는 함수.
    [[nodiscard]] Result<void> verify(const OperationPlan& plan);
private:
    /// 양쪽 실제 볼륨을 조회해 한 단계의 이동 계약을 만드는 함수.
    [[nodiscard]] Result<MoveContract> inspect(const MoveStep& step);
    ITransferVolumeInspector& volumes_;
};

} // namespace nookbit
