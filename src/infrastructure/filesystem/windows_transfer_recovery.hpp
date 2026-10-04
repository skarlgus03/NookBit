#pragma once
#include <nookbit/application/reconcile_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 기록 확정 동안 최종 파일과 부모를 보호하며 파일 변경은 제공하지 않는 어댑터.
class WindowsTransferRecoveryGuard final : public ITransferRecoveryGuard {
public:
    /// 원본·임시 파일 부재와 최종 복사본을 읽기 전용으로 보호하는 함수.
    [[nodiscard]] Result<std::unique_ptr<ITransferRecoverySession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) override;
};

/// 실행·되돌리기와 같은 시스템 잠금 안에서 명시적 완료 복구를 수행하는 진입점.
class TransferRecoveryService final {
public:
    /// 외부 소유의 복구 유스케이스를 빌려 받는 함수.
    explicit TransferRecoveryService(ReconcileTransferRecoveryUseCase& useCase) : useCase_(useCase) {}
    /// 다른 앱 인스턴스와 작업을 직렬화한 뒤 완료 기록 복구를 요청하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId);
private:
    ReconcileTransferRecoveryUseCase& useCase_;
};
} // namespace nookbit
