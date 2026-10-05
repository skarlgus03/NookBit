#pragma once

#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>
#include <nookbit/application/staged_copy.hpp>

namespace nookbit {

enum class InterruptedStepState { Pending, Moved, Restored, Skipped, NeedsAttention };

/// 후보 판단일 뿐 실행 허가가 아니며 선택 후 잠금 아래에서 반드시 다시 검사한다.
enum class RecoveryReadiness { NeedsRevalidation, NeedsStepRecovery, Blocked, NotApplicable };

struct RecoveryChoice {
    RecoveryReadiness readiness{RecoveryReadiness::Blocked};
    std::vector<Error> reasons;
};

struct InterruptedStepInspection {
    StepId stepId;
    std::filesystem::path source;
    std::filesystem::path destination;
    InterruptedStepState state{InterruptedStepState::NeedsAttention};
    std::optional<TransferRecoveryInspection> transfer;
    std::vector<Error> reasons;
};

/// 화면이나 캐릭터가 공유할 읽기 전용 전체 작업 진단 결과.
struct InterruptedPlanInspection {
    PlanId planId;
    PlanStatus status{PlanStatus::NeedsRecovery};
    std::optional<PlanStatus> interruptedStatus;
    bool planIntact{false};
    std::size_t pending{0}, moved{0}, restored{0}, skipped{0}, needsAttention{0};
    std::vector<InterruptedStepInspection> steps;
    RecoveryChoice resume;
    RecoveryChoice rollback;
    // 나중에는 아무 변경도 요청하지 않고 미해결 작업을 보존하는 선택이다.
};

/// 파일·DB 변경 권한 없이 중단된 혼합 작업을 요약하는 유스케이스.
class InspectInterruptedPlanUseCase final {
public:
    /// 외부 소유의 기록·파일 조회·해시·볼륨 조회 포트를 빌려 받는 함수.
    InspectInterruptedPlanUseCase(const IHistoryRepository& history, const ITransferJournal& journal,
        ITransferFileInspector& files, const IPlanHasher& hasher, ITransferVolumeInspector* volumes = nullptr);
    /// 작업 하나의 파일 상태와 복구 후보·차단 이유를 새로 읽어 반환하는 함수.
    [[nodiscard]] Result<InterruptedPlanInspection> execute(const PlanId& planId);
    /// 시작 시 미완료 목록 중 실제로 중단된 작업만 검사하는 함수.
    [[nodiscard]] Result<std::vector<InterruptedPlanInspection>> inspectAll();
private:
    const IHistoryRepository& history_;
    const ITransferJournal& journal_;
    ITransferFileInspector& files_;
    const IPlanHasher& hasher_;
    ITransferVolumeInspector* volumes_;
};
} // namespace nookbit
