#pragma once
#include <nookbit/application/ports.hpp>
namespace nookbit {
/// 확인 전 준비와 확인 후 승인·실행을 화면 객체 없이 수행하는 계약.
class ISortWorkflow {
public:
    /// 구현체를 안전하게 해제하는 함수.
    virtual ~ISortWorkflow() = default;
    /// 초안을 고정·재검사하고 파일 이동 없이 확인할 계획을 반환하는 함수.
    virtual Result<OperationPlan> prepare(const OperationPlan& plan, bool frozen,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) = 0;
    /// 명시적으로 확인한 계획을 다시 검사·승인하고 안전 실행하는 함수.
    virtual Result<void> approveAndExecute(const OperationPlan& plan,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) = 0;
};
}
