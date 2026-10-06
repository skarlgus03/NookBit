#pragma once
#include <nookbit/application/cleanup_transfer_staging_use_case.hpp>
#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>

namespace nookbit {
/// 조회 결과는 삭제 권한이 아니며 실행 때 같은 지문과 승인 해시를 다시 검사한다.
struct PreservedCleanupRequest {
    PreservedCleanupRecord expected;
    bool approved{false};
};
class IPreservedStagingCleanup {
public:
    /// 부분 임시 파일 정리 포트를 해제하는 함수.
    virtual ~IPreservedStagingCleanup() = default;
    /// 원본·기록된 임시 객체·부분 내용·승인 볼륨을 보호하고 삭제 전용 세션을 여는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<IStagingCleanupSession>> openPreserved(
        const TransferAttempt& attempt, const MoveContract& movement, const FileFingerprint& expected, bool priorIntent) = 0;
};
/// 호출자가 공유 작업 잠금을 보유해야 하며 조회와 명시적 실행을 분리한다.
class CleanupPreservedStagingUseCase final {
public:
    /// 외부 소유의 조회·감사·파일 관찰·보호 정리·해시 포트를 받는 함수.
    CleanupPreservedStagingUseCase(IHistoryRepository& history, const ITransferJournal& journal,
        ITransferFileInspector& files, IPreservedStagingCleanup& cleanup, const IPlanHasher& hasher);
    /// 보존 임시 파일을 변경 없이 읽고 승인 화면에 사용할 지문을 반환하는 함수.
    [[nodiscard]] Result<PreservedCleanupRecord> inspect(const PlanId& planId, const std::string& attemptId);
    /// 명시적 승인과 동일 지문을 재확인하고 의도 감사 뒤에만 임시 객체를 제거하는 함수.
    [[nodiscard]] Result<void> execute(const PreservedCleanupRequest& request);
private:
    /// 서명·완료된 되돌리기·보존 등록·정방향 증거를 묶어 검사하는 함수.
    [[nodiscard]] Result<MoveContract> bind(const PreservedCleanupRecord& request, const TransferAttempt& attempt);
    IHistoryRepository& history_;
    const ITransferJournal& journal_;
    ITransferFileInspector& files_;
    IPreservedStagingCleanup& cleanup_;
    const IPlanHasher& hasher_;
};
}
