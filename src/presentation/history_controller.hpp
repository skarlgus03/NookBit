#pragma once

#include <nookbit/application/file_operations.hpp>
#include <nookbit/application/history_query.hpp>

#include <QObject>
#include "presentation/background_task_runner.hpp"
#include <QString>
#include <QVariantList>

#include <filesystem>
#include <optional>

namespace nookbit {

/// 최근 작업 기록과 선택한 작업의 안전한 되돌리기를 화면에 제공하는 컨트롤러.
class HistoryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList historyEntries READ historyEntries NOTIFY historyChanged)
    Q_PROPERTY(QVariantList historySteps READ historySteps NOTIFY historyChanged)
    Q_PROPERTY(QString historyMessage READ historyMessage NOTIFY historyChanged)
    Q_PROPERTY(QString selectedHistoryPlanId READ selectedHistoryPlanId NOTIFY historyChanged)
    Q_PROPERTY(bool canUndoSelectedHistory READ canUndoSelectedHistory NOTIFY historyChanged)
    Q_PROPERTY(int selectedHistoryFileCount READ selectedHistoryFileCount NOTIFY historyChanged)
    Q_PROPERTY(bool hasMoreHistory READ hasMoreHistory NOTIFY historyChanged)
    Q_PROPERTY(QString searchMessage READ searchMessage NOTIFY historyChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY filtersChanged)
    Q_PROPERTY(QString period READ period NOTIFY filtersChanged)
    Q_PROPERTY(QString resultFilter READ resultFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder NOTIFY filtersChanged)
    Q_PROPERTY(QString fromDate READ fromDate NOTIFY filtersChanged)
    Q_PROPERTY(QString toDate READ toDate NOTIFY filtersChanged)

public:
    /// 앱 조립부의 단일 백그라운드 실행기를 빌려 받는 함수.
    void setTaskRunner(BackgroundTaskRunner* runner) { runner_ = runner; }
    /// 기록 조회와 외부 소유 파일 실행 계약으로 기록 화면을 준비하는 생성자.
    HistoryController(IHistoryRepository& history, IHistoryQuery& query, IFileOperations& operations, QObject* parent = nullptr);
    /// 이어 읽을 기록이 있는지 반환하는 함수.
    bool hasMoreHistory() const { return nextCursor_.has_value(); }
    /// 조회 결과 수 또는 조회 실패 안내를 반환하는 함수.
    QString searchMessage() const { return searchMessage_; }
    /// 마지막 성공한 검색어를 반환하는 함수.
    QString searchText() const { return searchText_; }
    /// 마지막 성공한 기간 프리셋을 반환하는 함수.
    QString period() const { return period_; }
    /// 마지막 성공한 작업 결과 필터를 반환하는 함수.
    QString resultFilter() const { return resultFilter_; }
    /// 마지막 성공한 정렬 순서를 반환하는 함수.
    QString sortOrder() const { return sortOrder_; }
    /// 마지막 성공한 직접 선택 시작일을 반환하는 함수.
    QString fromDate() const { return fromDate_; }
    /// 마지막 성공한 직접 선택 종료일을 반환하는 함수.
    QString toDate() const { return toDate_; }
    /// 화면 조건을 검증한 뒤 첫 페이지를 조회하고 실패하면 기존 결과를 유지하는 함수.
    Q_INVOKABLE bool applyHistoryFilters(const QString& text, const QString& period, const QString& result,
        const QString& order, const QString& from, const QString& to);
    /// 검색과 필터를 초기값으로 되돌려 전체 기록 첫 페이지를 읽는 함수.
    Q_INVOKABLE void resetHistoryFilters();
    /// 선택한 작업 상세는 유지하고 같은 조건의 다음 페이지를 추가하는 함수.
    Q_INVOKABLE void loadMoreHistory();

    /// 최근 실행 작업 목록을 화면에 반환하는 함수.
    [[nodiscard]] QVariantList historyEntries() const;

    /// 선택한 작업의 파일별 이동 기록을 반환하는 함수.
    [[nodiscard]] QVariantList historySteps() const;

    /// 기록 화면의 안내 문구를 반환하는 함수.
    [[nodiscard]] QString historyMessage() const;

    /// 선택한 작업 ID를 반환하는 함수.
    [[nodiscard]] QString selectedHistoryPlanId() const;

    /// 선택한 작업에 되돌릴 파일이 있는지 반환하는 함수.
    [[nodiscard]] bool canUndoSelectedHistory() const noexcept;

    /// 선택한 작업에서 되돌릴 파일 수를 반환하는 함수.
    [[nodiscard]] int selectedHistoryFileCount() const noexcept;

    /// 저장된 최근 실행 작업을 다시 읽는 함수.
    Q_INVOKABLE void refreshHistory();

    /// 기록에서 작업 하나를 골라 파일별 경로를 보여주는 함수.
    Q_INVOKABLE void selectHistoryPlan(const QString& planId);

    /// 선택한 과거 작업의 파일을 검증한 뒤 되돌리는 함수.
    Q_INVOKABLE void undoSelectedHistory();

signals:
    void historyChanged();
    void filtersChanged();
    void rollbackAttempted(const QString& planId);
    void recoveryRefreshRequested();

private:
    BackgroundTaskRunner* runner_{nullptr};
    IHistoryRepository& history_;
    IHistoryQuery& query_;
    IFileOperations& operations_;
    std::optional<OperationPlan> selectedHistoryPlan_;
    QVariantList historyEntries_;
    QVariantList historySteps_;
    QString historyMessage_;
    /// 한 페이지만 조회하고 성공한 값으로만 목록과 다음 위치를 갱신하는 함수.
    bool readSearchPage(const HistorySearch& query, bool append);
    HistorySearch activeSearch_;
    std::optional<HistoryCursor> nextCursor_;
    QString searchText_, period_{"all"}, resultFilter_{"all"}, sortOrder_{"newest"}, fromDate_, toDate_, searchMessage_;
};

} // namespace nookbit
