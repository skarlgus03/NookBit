#pragma once
#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>

namespace nookbit {
/// 실행기는 파일 복사 일꾼 대신 승인된 전송 작업 인터페이스를 호출한다.
class ITransferStepExecutor {
public:
    /// 외부에서 소유한 전송 실행 기능을 해제하는 함수.
    virtual ~ITransferStepExecutor() = default;
    /// 의도가 기록된 승인 단계 하나를 새 전송 시도로 실행하는 함수.
    [[nodiscard]] virtual Result<void> execute(const PlanId& planId, const StepId& stepId,
        const std::string& attemptId, const std::filesystem::path& staging) = 0;
};

/// 저장된 v2 승인 단계의 복사·검증·제거·원자적 완료를 조율한다.
class ExecuteTransferStepUseCase final : public ITransferStepExecutor {
public:
    /// 외부에서 소유한 파일·기록·서명 검증 포트를 빌려 받는 함수.
    ExecuteTransferStepUseCase(IStagedCopy& copy, ITransferSourceRemoval& removal,
        ITransferJournal& journal, const IHistoryRepository& history, const IPlanHasher& hasher);
    /// 호출자가 작업 잠금을 가진 상태에서 저장된 실행 의도 하나를 완료하는 함수.
    [[nodiscard]] Result<void> execute(const PlanId& planId, const StepId& stepId,
        const std::string& attemptId, const std::filesystem::path& staging) override;
private:
    IStagedCopy& copy_;
    ITransferSourceRemoval& removal_;
    ITransferJournal& journal_;
    const IHistoryRepository& history_;
    const IPlanHasher& hasher_;
};
}
