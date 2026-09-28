#pragma once

#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>

namespace nookbit {

/// 사용자의 명시적 승인 요청을 재검증 후 기록하는 계약.
class IApprovePlanUseCase {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IApprovePlanUseCase() = default;

    /// 선택 파일과 현재 설정을 재검증하고 승인을 기록하는 함수.
    [[nodiscard]] virtual Result<ApprovalRecord> execute(
        const PlanId& planId,
        const std::vector<std::filesystem::path>& selectedFiles,
        const UserSettings& settings) = 0;
};

} // namespace nookbit
