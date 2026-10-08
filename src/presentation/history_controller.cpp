#include "presentation/history_controller.hpp"


#include <QDateTime>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace nookbit {
namespace {

// Windows 경로를 Qt 문자열로 변환해 화면에 표시하는 함수.
QString displayPath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

// 저장된 작업 상태를 기록 화면의 한국어 문구로 바꾸는 함수.
QString historyStatusText(PlanStatus status) {
    switch (status) {
    case PlanStatus::Completed: return QStringLiteral("이동 완료");
    case PlanStatus::RolledBack: return QStringLiteral("되돌림 완료");
    case PlanStatus::Failed: return QStringLiteral("이동 실패");
    case PlanStatus::NeedsRecovery: return QStringLiteral("확인 필요");
    case PlanStatus::Executing: return QStringLiteral("실행 중단");
    case PlanStatus::RollingBack: return QStringLiteral("되돌리기 중단");
    default: return QStringLiteral("저장됨");
    }
}

// 저장된 파일 단계 상태를 기록 화면의 한국어 문구로 바꾸는 함수.
QString historyStepStatusText(StepStatus status) {
    switch (status) {
    case StepStatus::Applied: return QStringLiteral("이동됨");
    case StepStatus::RolledBack: return QStringLiteral("되돌림");
    case StepStatus::Skipped: return QStringLiteral("건너뜀");
    case StepStatus::Failed: return QStringLiteral("실패");
    case StepStatus::NeedsRecovery: return QStringLiteral("확인 필요");
    default: return QStringLiteral("미완료");
    }
}

} // namespace

// 기록 조회와 외부 소유 파일 실행 계약으로 기록 화면을 준비하는 생성자.
HistoryController::HistoryController(IHistoryRepository& history, IFileOperations& operations, QObject* parent)
    : QObject(parent), history_(history), operations_(operations) {}

// 최근 실행 작업 목록을 화면에 반환하는 함수.
QVariantList HistoryController::historyEntries() const { return historyEntries_; }

// 선택한 작업의 파일별 이동 기록을 반환하는 함수.
QVariantList HistoryController::historySteps() const { return historySteps_; }

// 기록 화면의 안내 문구를 반환하는 함수.
QString HistoryController::historyMessage() const { return historyMessage_; }

// 선택한 작업 ID를 반환하는 함수.
QString HistoryController::selectedHistoryPlanId() const {
    return selectedHistoryPlan_ ? QString::fromStdString(selectedHistoryPlan_->id) : QString();
}

// 선택한 작업에 되돌릴 파일이 있는지 반환하는 함수.
bool HistoryController::canUndoSelectedHistory() const noexcept {
    if (!selectedHistoryPlan_ ||
        (selectedHistoryPlan_->status != PlanStatus::Completed &&
         selectedHistoryPlan_->status != PlanStatus::Failed)) return false;
    return std::any_of(selectedHistoryPlan_->steps.begin(), selectedHistoryPlan_->steps.end(),
        [](const MoveStep& step) { return step.status == StepStatus::Applied; });
}

// 선택한 작업에서 되돌릴 파일 수를 반환하는 함수.
int HistoryController::selectedHistoryFileCount() const noexcept {
    if (!selectedHistoryPlan_) return 0;
    return static_cast<int>(std::count_if(
        selectedHistoryPlan_->steps.begin(), selectedHistoryPlan_->steps.end(),
        [](const MoveStep& step) { return step.status == StepStatus::Applied; }));
}

// 최근 실행 작업 목록을 SQLite에서 다시 읽는 함수.
void HistoryController::refreshHistory() {
    if (runner_ && runner_->busy()) return;
    selectedHistoryPlan_.reset();
    historySteps_.clear();
    historyEntries_.clear();
    const auto result = history_.recentPlans(50);
    if (!result) {
        historyMessage_ = QStringLiteral("작업 기록을 읽지 못했어요: %1")
                              .arg(QString::fromStdString(result.error().message));
        emit historyChanged();
        return;
    }
    for (const auto& entry : result.value()) {
        const auto utc = QDateTime::fromString(
            QString::fromStdString(entry.lastActivityUtc), Qt::ISODateWithMs);
        QVariantMap row;
        row.insert(QStringLiteral("planId"), QString::fromStdString(entry.id));
        row.insert(QStringLiteral("time"), utc.isValid()
            ? utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"))
            : QString::fromStdString(entry.lastActivityUtc));
        row.insert(QStringLiteral("status"), historyStatusText(entry.status));
        row.insert(QStringLiteral("fileCount"), entry.fileCount);
        row.insert(QStringLiteral("appliedCount"), entry.appliedCount);
        historyEntries_.append(row);
    }
    historyMessage_ = historyEntries_.isEmpty()
        ? QStringLiteral("아직 이동 작업 기록이 없어요.")
        : QStringLiteral("최근 작업 %1건을 표시해요. 작업을 선택하면 파일별 경로를 볼 수 있어요.")
              .arg(historyEntries_.size());
    emit historyChanged();
}

// 기록에서 작업 하나를 골라 저장된 파일별 경로를 보여주는 함수.
void HistoryController::selectHistoryPlan(const QString& planId) {
    if (runner_ && runner_->busy()) return;
    selectedHistoryPlan_.reset();
    historySteps_.clear();
    const bool listed = std::any_of(historyEntries_.begin(), historyEntries_.end(),
        [&planId](const QVariant& entry) {
            return entry.toMap().value(QStringLiteral("planId")).toString() == planId;
        });
    if (!listed) {
        historyMessage_ = QStringLiteral("목록을 다시 불러온 뒤 작업을 선택해 주세요.");
        emit historyChanged();
        return;
    }
    const auto loaded = history_.loadPlan(planId.toStdString());
    if (!loaded) {
        historyMessage_ = QStringLiteral("선택한 작업을 읽지 못했어요: %1")
                              .arg(QString::fromStdString(loaded.error().message));
        emit historyChanged();
        return;
    }
    selectedHistoryPlan_ = loaded.value();
    for (const auto& step : selectedHistoryPlan_->steps) {
        QVariantMap row;
        row.insert(QStringLiteral("fileName"), displayPath(step.source.filename()));
        row.insert(QStringLiteral("source"), displayPath(step.source));
        row.insert(QStringLiteral("destination"), displayPath(step.destination));
        row.insert(QStringLiteral("status"), historyStepStatusText(step.status));
        if (step.failure) row["status"] = historyStepStatusText(step.status) + QStringLiteral(": %1 (%2)")
            .arg(QString::fromStdString(step.failure->message), QString::fromStdString(step.failure->code));
        historySteps_.append(row);
    }
    historyMessage_ = canUndoSelectedHistory()
        ? QStringLiteral("원본과 목적지를 확인한 뒤 되돌리기를 선택해 주세요. 파일이 바뀌었으면 안전 검사가 중단해요.")
        : QStringLiteral("이 작업은 현재 기록 상태에서 되돌릴 이동 파일이 없어요.");
    emit historyChanged();
}

// 선택한 과거 작업의 파일을 재검증하고 안전한 되돌리기를 실행하는 함수.
void HistoryController::undoSelectedHistory() {
    if (runner_ && runner_->busy()) return;
    if (!canUndoSelectedHistory()) {
        historyMessage_ = QStringLiteral("되돌릴 수 있는 작업을 먼저 선택해 주세요.");
        emit historyChanged();
        return;
    }
    const auto planId = selectedHistoryPlan_->id;
    dispatchOperation(runner_, this, [operations = &operations_, planId] { return operations->rollback(planId); },
        [this, planId](const Result<void>& result) {
        emit rollbackAttempted(QString::fromStdString(planId));
        refreshHistory();
        if (!result) {
            const auto current = history_.loadPlan(planId);
            if (current && current.value().status == PlanStatus::NeedsRecovery) {
                emit recoveryRefreshRequested();
            }
            historyMessage_ = QStringLiteral("되돌리지 못했어요. 파일은 덮어쓰지 않았어요: %1")
                                  .arg(QString::fromStdString(result.error().message));
        } else {
            historyMessage_ = QStringLiteral("선택한 작업의 파일을 원래 위치로 되돌렸어요.");
        }
        emit historyChanged();
    }, BackgroundTaskRunner::TaskKind::FileWork);
}

} // namespace nookbit
