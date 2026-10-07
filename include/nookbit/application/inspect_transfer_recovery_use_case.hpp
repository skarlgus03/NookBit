#pragma once

#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>

namespace nookbit {

enum class TransferPathPresence { Missing, Present, Unavailable };

/// 읽기 전용 검사 시점의 관찰값이며 파일 작업을 허가하는 증거는 아니다.
struct TransferFileObservation {
    TransferPathPresence presence{TransferPathPresence::Unavailable};
    std::optional<std::string> volumeId;
    std::optional<FileFingerprint> fingerprint;
    std::optional<Error> error;
};

class ITransferFileInspector {
public:
    /// 읽기 전용 검사 일꾼을 해제하는 함수.
    virtual ~ITransferFileInspector() = default;
    /// 볼륨·객체·내용을 읽되 파일 생성·이동·삭제는 하지 않는 함수.
    [[nodiscard]] virtual TransferFileObservation inspect(const std::filesystem::path& path) = 0;
    /// 다운로드 표식 보존 계약의 완료 작업을 읽기 전용으로 검사하는 함수.
    [[nodiscard]] virtual TransferFileObservation inspectWithZone(const std::filesystem::path& path) { return inspect(path); }
    /// 원본 없이 남은 최종 파일의 표식·속성을 저장 증거와 읽기 전용 대조하는 함수.
    [[nodiscard]] virtual Result<void> checkRemovalProof(const std::filesystem::path&, const TransferRemovalProof&) {
        return Result<void>::failure({"recovery.zone_inspector_required", "Persisted removal proof inspection is required."});
    }
    /// 두 파일의 다운로드 표식을 비교하며 부분 복사에서만 복사본 표식 부재를 허용하는 함수.
    [[nodiscard]] virtual Result<void> checkDownloadMarkers(const std::filesystem::path&,
        const std::filesystem::path&, bool = false) {
        return Result<void>::failure({"recovery.zone_inspector_required", "Download marker comparison is required."});
    }
};

enum class TransferRecoveryFileState {
    Missing, Matches, ObjectOnly, Unowned, Changed, OtherVolume, Unavailable,
};

enum class TransferRecoveryFinding {
    NoCopyObserved, PartialCopy, VerifiedStaging, CommittedSourceRetained,
    CommitNotRecorded, SourceAbsentAfterRemoveIntent, ChangedFile,
    Ambiguous, Unavailable, UntrustedRecord,
};

struct TransferRecoveryInspection {
    TransferAttempt attempt;
    TransferRecoveryFileState source{TransferRecoveryFileState::Unavailable};
    TransferRecoveryFileState staging{TransferRecoveryFileState::Unavailable};
    TransferRecoveryFileState destination{TransferRecoveryFileState::Unavailable};
    TransferRecoveryFinding finding{TransferRecoveryFinding::UntrustedRecord};
    /// 화면·로그에서 쓸 설명이며 자동 복구 명령이 아니다.
    std::string reason;
    std::vector<Error> observationErrors;
};

/// 전송 기록과 세 파일 위치를 대조할 뿐 파일·저널·상위 상태는 변경하지 않는다.
class InspectTransferRecoveryUseCase final {
public:
    /// 외부에서 소유하는 이력·저널·파일 검사·해시 포트를 빌려 받는 함수.
    InspectTransferRecoveryUseCase(const IHistoryRepository& history, const ITransferJournal& journal,
        ITransferFileInspector& files, const IPlanHasher& hasher);
    /// 새로 읽은 기록과 파일 상태로 중단 시도 하나를 검사하는 함수.
    [[nodiscard]] Result<TransferRecoveryInspection> execute(const std::string& attemptId);
private:
    const IHistoryRepository& history_;
    const ITransferJournal& journal_;
    ITransferFileInspector& files_;
    const IPlanHasher& hasher_;
};

} // namespace nookbit
