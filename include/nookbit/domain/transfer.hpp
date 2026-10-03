#pragma once

#include <nookbit/domain/model.hpp>

namespace nookbit {

enum class TransferDirection { Forward, Rollback };

/// 한 전송 시도의 기록 단계이며 파일 작업을 허가하는 권한은 아니다.
enum class TransferPhase {
    Prepared, CopyIntent, StagingCreated, CopyVerified, CommitIntent,
    DestinationCommitted, SourceRemoveIntent, SourceRemoved, Completed,
    Failed, Cancelled, NeedsRecovery,
};

/// 승인 계획과 분리해 보관할 정방향 또는 역방향 전송 시도.
struct TransferAttempt {
    std::string id;
    PlanId planId;
    StepId stepId;
    TransferDirection direction{TransferDirection::Forward};
    std::filesystem::path source;
    std::filesystem::path destination;
    std::filesystem::path staging;
    FileFingerprint sourceFingerprint;
    std::optional<FileFingerprint> stagingFingerprint;
    std::optional<FileFingerprint> destinationFingerprint;
    TransferPhase phase{TransferPhase::Prepared};
    std::optional<Error> error;
};

/// 예상한 이전 단계와 새 증거를 함께 전달하는 낙관적 동시성 갱신 요청.
struct TransferUpdate {
    TransferPhase expectedPhase{TransferPhase::Prepared};
    TransferPhase nextPhase{TransferPhase::CopyIntent};
    std::optional<FileFingerprint> stagingFingerprint;
    std::optional<FileFingerprint> destinationFingerprint;
    std::optional<Error> error;
};

/// 내용 해시 비교와 객체 ID 비교를 구분하는 전송 계약.
namespace TransferValidation {

/// 복사본의 크기와 유효한 SHA-256 내용이 같은지 검사하는 함수.
[[nodiscard]] bool sameContent(const FileFingerprint& left, const FileFingerprint& right);

/// 파일 객체의 볼륨 ID와 파일 ID가 같은지 검사하는 함수.
[[nodiscard]] bool sameObject(const FileFingerprint& left, const FileFingerprint& right);

/// 순서 건너뛰기·완료 상태 재사용 없이 단계 전이가 가능한지 검사하는 함수.
[[nodiscard]] bool canAdvance(TransferPhase from, TransferPhase to);

/// 오류·취소·완료로 종료한 시도인지 반환하는 함수.
[[nodiscard]] bool isTerminal(TransferPhase phase);

/// 새 전송 시도의 경로와 원본 지문·초기 상태를 검사하는 함수.
[[nodiscard]] Result<void> validateNew(const TransferAttempt& attempt);

/// 허용된 증거만 합쳐 다음 단계의 스냅샷을 만드는 함수.
[[nodiscard]] Result<TransferAttempt> advance(const TransferAttempt& attempt, const TransferUpdate& update);

} // namespace TransferValidation
} // namespace nookbit
