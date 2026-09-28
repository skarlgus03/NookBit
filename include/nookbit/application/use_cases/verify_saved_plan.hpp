#pragma once

#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>

namespace nookbit {

/// 저장 계획의 재검증 결과만 다른 유스케이스에 전달하는 계약.
class IVerifySavedPlanUseCase {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IVerifySavedPlanUseCase() = default;

    /// 저장 계획과 현재 파일·규칙·설정의 일치 여부를 확인하는 함수.
    [[nodiscard]] virtual Result<OperationPlan> execute(
        const PlanId& planId,
        const std::vector<std::filesystem::path>& selectedFiles,
        PlanStatus expectedStatus = PlanStatus::Planned) = 0;
};

} // namespace nookbit
