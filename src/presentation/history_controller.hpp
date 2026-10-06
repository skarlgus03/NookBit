#pragma once

#include <nookbit/application/file_operations.hpp>

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

public:
    /// 앱 조립부의 단일 백그라운드 실행기를 빌려 받는 함수.
    void setTaskRunner(BackgroundTaskRunner* runner) { runner_ = runner; }
    /// 기록 조회와 외부 소유 파일 실행 계약으로 기록 화면을 준비하는 생성자.
    HistoryController(IHistoryRepository& history, IFileOperations& operations, QObject* parent = nullptr);

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
    void rollbackAttempted(const QString& planId);
    void recoveryRefreshRequested();

private:
    BackgroundTaskRunner* runner_{nullptr};
    IHistoryRepository& history_;
    IFileOperations& operations_;
    std::optional<OperationPlan> selectedHistoryPlan_;
    QVariantList historyEntries_;
    QVariantList historySteps_;
    QString historyMessage_;
};

} // namespace nookbit
