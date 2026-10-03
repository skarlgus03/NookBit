#pragma once

#include <nookbit/domain/transfer.hpp>

namespace nookbit {

/// 파일을 변경하지 않고 전송 시도와 단계별 증거만 기록하는 계약.
class ITransferJournal {
public:
    /// 구현체를 안전하게 정리하는 소멸자.
    virtual ~ITransferJournal() = default;

    /// 기존 실행 단계에 연결된 새 전송 시도를 기록하는 함수.
    [[nodiscard]] virtual Result<void> begin(const TransferAttempt& attempt) = 0;

    /// 예상한 이전 단계가 같을 때만 증거와 다음 단계를 원자적으로 기록하는 함수.
    [[nodiscard]] virtual Result<void> advance(const std::string& attemptId, const TransferUpdate& update) = 0;

    /// 재시작 후 전송 시도의 마지막 스냅샷을 복원하는 함수.
    [[nodiscard]] virtual Result<TransferAttempt> load(const std::string& attemptId) const = 0;

    /// 단계 하나의 정방향·역방향 시도를 생성 순서대로 읽는 함수.
    [[nodiscard]] virtual Result<std::vector<TransferAttempt>> forStep(const StepId& stepId) const = 0;
};

} // namespace nookbit
