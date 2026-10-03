#pragma once

#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>
#include "infrastructure/filesystem/file_identity.hpp"

namespace nookbit {

/// 시작 시 확인한 미완료 계획의 결과 개수를 모은다.
struct RecoveryReport {
    std::size_t reconciled{0};
    std::vector<PlanId> needsAttention;
};

/// 사용자가 검토할 중단 작업의 실제 파일 상태를 담는다.
struct RecoveryCase {
    PlanId planId;
    std::size_t partIndex{0};
    std::size_t partCount{0};
    std::filesystem::path source;
    std::filesystem::path destination;
    FileProbe sourceState{FileProbe::Unavailable};
    FileProbe destinationState{FileProbe::Unavailable};
    bool contentHashAvailable{false};
    bool planIntact{false};
    bool originKnown{false};
    bool canResolve{false};
    bool transferTracked{false};
};

/// 중단된 작업의 각 파일 위치와 DB 상태를 대조하는 서비스.
class RecoveryService final {
public:
    /// 이력과 현재 파일 정보를 확인할 기능을 받는 생성자.
    RecoveryService(IHistoryRepository& history, IScanner& scanner, IPlanHasher& hasher,
        const ITransferJournal* transfers = nullptr);

    /// 파일 이동 없이 확실한 상태만 DB에 반영하고 불확실한 계획은 표시하는 함수.
    [[nodiscard]] Result<RecoveryReport> reconcile();

    /// 확인 필요 작업의 두 파일 위치와 복구 가능 여부를 읽는 함수.
    [[nodiscard]] Result<std::vector<RecoveryCase>> inspectCases();

    /// 사용자가 요청한 작업을 다시 검사해 확실한 기록만 정리하는 함수.
    [[nodiscard]] Result<void> resolveCase(const PlanId& planId);

private:
    /// 다른 볼륨 전송 기록이 있어 기존 두 위치 복구를 금지해야 하는지 검사하는 함수.
    [[nodiscard]] Result<bool> hasTransfers(const OperationPlan& plan) const;
    /// 불확실한 계획을 NeedsRecovery 상태로 기록하는 함수.
    [[nodiscard]] Result<void> markNeedsRecovery(const OperationPlan& plan);

    IHistoryRepository& history_;
    IScanner& scanner_;
    IPlanHasher& hasher_;
    const ITransferJournal* transfers_;
};

} // namespace nookbit
