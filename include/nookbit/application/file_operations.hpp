#pragma once
#include <nookbit/application/ports.hpp>

namespace nookbit {
/// 화면에 파일 일꾼의 생성·소유 책임을 주지 않는 승인 실행 계약.
class IFileOperations {
public:
    /// 실행 계약 구현을 안전하게 해제하는 함수.
    virtual ~IFileOperations() = default;
    /// 화면에서 승인한 계획과 현재 선택·설정으로 안전한 이동을 요청하는 함수.
    [[nodiscard]] virtual Result<void> executeApproved(const OperationPlan& plan, const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) = 0;
    /// 저장된 승인 계약으로 완료된 이동을 안전하게 되돌리는 함수.
    [[nodiscard]] virtual Result<void> rollback(const PlanId& planId) = 0;
};
}
