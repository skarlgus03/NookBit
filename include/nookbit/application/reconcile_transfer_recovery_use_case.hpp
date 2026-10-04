#pragma once

#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>
#include <memory>

namespace nookbit {

/// 파일과 부모 핸들을 완료 기록 저장이 끝날 때까지 보유하는 읽기 전용 세션.
class ITransferRecoverySession {
public:
    /// 파일을 변경하지 않고 보호 핸들을 해제하는 함수.
    virtual ~ITransferRecoverySession() = default;
    /// 원본·임시 파일 부재와 최종 객체·내용·볼륨을 보호된 상태에서 재검증하는 함수.
    [[nodiscard]] virtual Result<void> verify() = 0;
};

class ITransferRecoveryGuard {
public:
    /// 복구 검사 일꾼을 해제하는 함수.
    virtual ~ITransferRecoveryGuard() = default;
    /// 승인 계약과 파일 증거를 보호하며 생성·이동·삭제 없는 검사 세션을 여는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransferRecoverySession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) = 0;
};

/// 호출자가 실행과 공유하는 작업 잠금을 보유해야 하며 미완료 복사는 재개하지 않는다.
class ReconcileTransferRecoveryUseCase final {
public:
    /// 외부 소유 이력·저널·보호 검사·해시 포트를 빌려 받는 함수.
    ReconcileTransferRecoveryUseCase(const IHistoryRepository& history, ITransferJournal& journal,
        ITransferRecoveryGuard& guard, const IPlanHasher& hasher);
    /// 이동이 이미 끝난 중단 시도의 완료 기록만 명시적으로 복구하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId);
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    ITransferRecoveryGuard& guard_;
    const IPlanHasher& hasher_;
};
} // namespace nookbit
