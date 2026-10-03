#pragma once

#include <nookbit/application/transfer_journal.hpp>
#include <QSqlDatabase>
#include <QString>

namespace nookbit {

/// 승인 계획과 별개인 전송 스냅샷·단계 이벤트를 보관하는 SQLite 일꾼.
class SqliteTransferJournal final : public ITransferJournal {
public:
    /// 기존 이력과 공유할 DB 및 초기 마이그레이션 경로를 받는 생성자.
    SqliteTransferJournal(QString databasePath, QString migrationPath);
    /// 자신이 연 연결만 닫는 소멸자.
    ~SqliteTransferJournal() override;
    /// 실행 중인 단계에 연결된 새 시도를 기록하는 함수.
    [[nodiscard]] Result<void> begin(const TransferAttempt& attempt) override;
    /// 예상 상태·증거를 검사하고 스냅샷과 이벤트를 함께 기록하는 함수.
    [[nodiscard]] Result<void> advance(const std::string& attemptId, const TransferUpdate& update) override;
    /// 정방향 전송 완료와 상위 단계·계획 기록을 한 트랜잭션으로 저장하는 함수.
    [[nodiscard]] Result<void> completeForward(const std::string& attemptId) override;
    /// 이벤트를 순서대로 재생해 마지막 스냅샷을 검증·복원하는 함수.
    [[nodiscard]] Result<TransferAttempt> load(const std::string& attemptId) const override;
    /// 한 단계에 속한 시도를 생성 순서대로 읽는 함수.
    [[nodiscard]] Result<std::vector<TransferAttempt>> forStep(const StepId& stepId) const override;

private:
    /// 연결을 열고 공유 Schema를 지원 버전까지 이행하는 함수.
    [[nodiscard]] Result<void> ensureOpen() const;
    /// 현재 트랜잭션에 단계별 증거 이벤트를 추가하는 함수.
    [[nodiscard]] Result<void> appendSnapshot(const TransferAttempt& attempt) const;
    /// 이미 열린 트랜잭션 안에서 스냅샷과 이벤트를 함께 읽는 함수.
    [[nodiscard]] Result<TransferAttempt> readAttempt(const std::string& attemptId) const;
    QString databasePath_;
    QString migrationPath_;
    QString connectionName_;
    mutable QSqlDatabase database_;
};

} // namespace nookbit
