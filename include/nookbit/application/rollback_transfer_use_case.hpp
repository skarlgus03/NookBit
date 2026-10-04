#pragma once
#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 실행기가 완료된 전송 단계의 안전한 역전송을 요청하는 인터페이스.
class IRollbackTransferStepExecutor {
public:
    /// 외부에서 소유한 역전송 실행 기능을 해제하는 함수.
    virtual ~IRollbackTransferStepExecutor() = default;
    /// 작업 잠금을 가진 호출자가 완료된 단계 하나를 새 시도로 되돌리는 함수.
    [[nodiscard]] virtual Result<void> rollback(const PlanId& planId, const StepId& stepId,
        const std::string& attemptId, const std::filesystem::path& staging) = 0;
    /// 배치의 파일을 변경하기 전에 역전송 가능 여부만 검사하는 함수.
    [[nodiscard]] virtual Result<void> verifyRollback(const PlanId&, const StepId&) {
        return Result<void>::failure({"transfer.preflight_unsupported", "Read-only reverse preflight is required."});
    }
};
/// 완료된 전송 결과를 원본으로 삼아 같은 안전 절차로 역전송하는 유스케이스.
class RollbackTransferUseCase final : public IRollbackTransferStepExecutor {
public:
    /// 외부에서 소유한 전송·검증·기록 포트를 빌려 받는 함수.
    RollbackTransferUseCase(ITransferJournal& journal, IHistoryRepository& history,
        IStagedCopy& copy, ITransferSourceRemoval& removal, ITransferFileInspector& files,
        const IPlanHasher& hasher);
    /// 원래 위치가 비어 있고 이동 결과가 그대로인 단계만 새 시도로 되돌리는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& forwardAttemptId,
        const std::string& rollbackAttemptId, const std::filesystem::path& staging,
        const std::function<bool()>& cancelled = {});
    /// 해당 단계의 완료된 정방향 결과를 찾아 승인 기반 역전송을 요청하는 함수.
    [[nodiscard]] Result<void> rollback(const PlanId& planId, const StepId& stepId,
        const std::string& attemptId, const std::filesystem::path& staging) override;
    /// 기록·볼륨·충돌·결과 지문을 검사하되 파일과 DB를 변경하지 않는 함수.
    [[nodiscard]] Result<void> verifyRollback(const PlanId& planId, const StepId& stepId) override;
private:
    /// 해당 단계에 연결된 유일한 완료 정방향 시도 ID를 찾는 함수.
    [[nodiscard]] Result<std::string> findForward(const PlanId& planId, const StepId& stepId);
    /// 동일한 사전 검사를 공유하고 실행 요청일 때만 역전송 의도를 기록하는 함수.
    [[nodiscard]] Result<TransferAttempt> executeImpl(const std::string& forwardAttemptId,
        const std::string& rollbackAttemptId, const std::filesystem::path& staging,
        const std::function<bool()>& cancelled, bool inspectOnly);
    ITransferJournal& journal_;
    IHistoryRepository& history_;
    IStagedCopy& copy_;
    ITransferSourceRemoval& removal_;
    ITransferFileInspector& files_;
    const IPlanHasher& hasher_;
};
} // namespace nookbit
