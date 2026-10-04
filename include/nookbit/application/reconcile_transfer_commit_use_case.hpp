#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>
#include <functional>
#include <memory>

namespace nookbit {
/// 원본과 최종 객체·부모를 기록 저장까지 보호하며 파일을 변경하지 않는 세션.
class ITransferCommitRecoverySession {
public:
    /// 보호 핸들만 해제하고 파일은 변경하지 않는 함수.
    virtual ~ITransferCommitRecoverySession() = default;
    /// 원본·최종 내용과 임시 부재를 재검사하고 최종 지문을 반환하는 함수.
    [[nodiscard]] virtual Result<FileFingerprint> verify(const std::function<bool()>& cancelled = {}) = 0;
};
class ITransferCommitRecoveryGuard {
public:
    /// 검사 일꾼을 안전하게 해제하는 함수.
    virtual ~ITransferCommitRecoveryGuard() = default;
    /// 기존 원본·최종 파일을 읽기 전용으로 보호하는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransferCommitRecoverySession>> open(
        const TransferAttempt& attempt, const MoveContract* movement) = 0;
};
/// 확정 결과 누락을 명시적으로 복구하며 파일 이동·삭제와 상위 완료는 하지 않는다.
class ReconcileTransferCommitUseCase final {
public:
    /// 외부 소유 기록·보호 검사·서명 포트를 빌려 받는 함수.
    ReconcileTransferCommitUseCase(const IHistoryRepository& history, ITransferJournal& journal,
        ITransferCommitRecoveryGuard& guard, const IPlanHasher& hasher);
    /// 확정 의도·승인과 두 파일을 대조해 누락된 최종 증거만 기록하는 함수.
    [[nodiscard]] Result<TransferAttempt> execute(const std::string& attemptId, const std::function<bool()>& cancelled = {});
private:
    const IHistoryRepository& history_;
    ITransferJournal& journal_;
    ITransferCommitRecoveryGuard& guard_;
    const IPlanHasher& hasher_;
};
}
