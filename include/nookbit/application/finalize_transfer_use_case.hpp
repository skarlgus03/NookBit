#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>
#include <functional>
#include <memory>

namespace nookbit {
class ITransferRemovalSession {
public:
    /// 제거 예약 없이 핸들만 해제하는 함수.
    virtual ~ITransferRemovalSession() = default;
    /// 보호된 원본과 최종 복사본을 다시 검증하는 함수.
    [[nodiscard]] virtual Result<void> verify() = 0;
    /// 같은 보호 세션의 검증된 원본만 한 번 제거하고 실제 부재를 확인하는 함수.
    [[nodiscard]] virtual Result<void> removeVerifiedSource() = 0;
};
class ITransferSourceRemoval {
public:
    /// 제거 일꾼을 해제하는 함수.
    virtual ~ITransferSourceRemoval() = default;
    /// 확정 증거가 있는 원본·최종 파일을 보호하고 검증하는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransferRemovalSession>> open(const TransferAttempt& attempt) = 0;
};

/// 최종 복사본이 기록된 정방향 전송의 원본 제거와 완료 기록만 조율한다.
class FinalizeTransferUseCase final {
public:
    /// 외부에서 소유한 기록·파일 제거·계획 검증 포트를 빌려 받는 함수.
    FinalizeTransferUseCase(ITransferJournal& journal, ITransferSourceRemoval& removal,
        const IHistoryRepository& history, const IPlanHasher& hasher);
    /// 다시 검증한 전송만 제거 의도·실제 제거·완료 기록 순서로 처리하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId,
        const std::function<bool()>& cancelled = {});
private:
    ITransferJournal& journal_;
    ITransferSourceRemoval& removal_;
    const IHistoryRepository& history_;
    const IPlanHasher& hasher_;
};
} // namespace nookbit
