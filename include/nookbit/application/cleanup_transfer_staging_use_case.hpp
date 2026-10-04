#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>
#include <memory>

namespace nookbit {
class IStagingCleanupSession {
public:
    /// 제거 예약 없이 보호 핸들만 해제하는 함수.
    virtual ~IStagingCleanupSession() = default;
    /// 원본과 임시 객체·내용·볼륨 및 최종 경로 부재를 재검증하는 함수.
    [[nodiscard]] virtual Result<void> verify() = 0;
    /// 기록된 정리 의도 아래 검증된 임시 객체만 제거하고 부재를 확인하는 함수.
    [[nodiscard]] virtual Result<void> removeVerifiedStaging() = 0;
};
class IStagingCleanup {
public:
    /// 임시 정리 일꾼을 해제하는 함수.
    virtual ~IStagingCleanup() = default;
    /// 원본을 보호하고 검증된 임시 파일의 삭제 전용 세션을 여는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<IStagingCleanupSession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) = 0;
};

/// 호출자가 실행·복구와 공유하는 작업 잠금을 보유해야 하는 명시적 정리 유스케이스.
class CleanupTransferStagingUseCase final {
public:
    /// 외부 소유 이력·저널·정리·해시 포트를 빌려 받는 함수.
    CleanupTransferStagingUseCase(const IHistoryRepository& history, ITransferJournal& journal,
        IStagingCleanup& cleanup, const IPlanHasher& hasher);
    /// 중단된 전송의 검증된 임시 복사본만 정리하고 이동 상태는 유지하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId);
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    IStagingCleanup& cleanup_;
    const IPlanHasher& hasher_;
};
} // namespace nookbit
