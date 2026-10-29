#pragma once

#include <nookbit/application/ports.hpp>
#include <nookbit/application/history_query.hpp>

#include <QSqlDatabase>
#include <QString>

namespace nookbit {

/// 승인 전 계획을 앱 데이터 폴더의 SQLite에 저장하는 이력 저장소.
class SqliteHistoryRepository final : public IHistoryRepository, public IHistoryQuery {
public:
    /// DB 파일과 초기 Schema 경로를 보관하는 생성자.
    SqliteHistoryRepository(QString databasePath, QString migrationPath);

    /// 자신이 연 DB 연결을 닫는 소멸자.
    ~SqliteHistoryRepository() override;

    /// Planned 계획과 단계들을 한 트랜잭션에 저장하는 함수.
    [[nodiscard]] Result<void> savePlan(const OperationPlan& plan) override;

    /// 해시가 같은 Planned 계획 하나를 Approved로 기록하는 함수.
    [[nodiscard]] Result<void> saveApproval(const ApprovalRecord& approval) override;

    /// 실행 상태 변경과 이벤트를 한 트랜잭션에 기록하는 함수.
    [[nodiscard]] Result<void> appendEvent(const DomainEvent& event) override;

    /// 검사한 승인·단계 상태를 비교하고 복구 선택과 재시작 상태를 원자적으로 저장하는 함수.
    [[nodiscard]] Result<void> beginRecovery(const RecoveryRestart& restart) override;
    /// 선택 감사에 남긴 보존 임시 파일의 추적 정보를 읽는 함수.
    [[nodiscard]] Result<std::vector<PreservedTransfer>> preservedTransfers(const PlanId& planId) const override;
    /// 별도 부분 정리의 승인 지문과 완료 감사를 검증해 읽는 함수.
    [[nodiscard]] Result<std::optional<PreservedCleanupRecord>> preservedCleanup(const PlanId& planId, const std::string& attemptId) const override;
    /// 부분 정리의 최초 의도 또는 동일 승인 지문의 완료만 원자적으로 기록하는 함수.
    [[nodiscard]] Result<void> recordPreservedCleanup(const PreservedCleanupRecord& record) override;
    /// 완료된 되돌리기에서도 보존 임시 파일을 확인할 작업을 읽는 함수.
    [[nodiscard]] Result<std::vector<PlanId>> preservedTransferPlans() const override;

    /// 아직 완료되지 않은 저장 계획을 읽어오는 함수.
    [[nodiscard]] Result<std::vector<OperationPlan>> incompletePlans() const override;

    /// 완료된 계획까지 포함해 ID 하나로 읽는 함수.
    [[nodiscard]] Result<OperationPlan> loadPlan(const PlanId& planId) const override;

    /// 최근 실행 작업을 파일을 변경하지 않고 최신순으로 조회하는 함수.
    [[nodiscard]] Result<std::vector<PlanHistoryEntry>> recentPlans(int limit) const override;
    /// 경로·기간·결과 조건과 안정적인 이어 읽기로 기록을 조회하는 함수.
    [[nodiscard]] Result<HistorySearchPage> searchHistory(const HistorySearch& query) const override;

    /// 확인 필요 상태 전의 실행·되돌리기 상태를 이벤트 기록에서 찾는 함수.
    [[nodiscard]] Result<PlanStatus> interruptedStatus(const PlanId& planId) const override;

private:
    /// 데이터베이스를 열고 초기 Schema를 적용하는 함수.
    [[nodiscard]] Result<void> ensureOpen() const;

    /// 주어진 조건에 맞는 계획과 단계들을 DB에서 복원하는 함수.
    [[nodiscard]] Result<std::vector<OperationPlan>> readPlans(
        const QString& whereClause, const QString& planId = {}) const;

    QString databasePath_;
    QString migrationPath_;
    QString connectionName_;
    mutable QSqlDatabase database_;
};

} // namespace nookbit
