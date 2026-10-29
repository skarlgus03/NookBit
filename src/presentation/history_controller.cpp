#include "presentation/history_controller.hpp"
#include "presentation/history_date_group.hpp"


#include <QDateTime>
#include <QTimeZone>
#include <QSet>
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

// 기록의 저장 상태에서 화면의 의미색만 결정하는 함수.
QString historyStatusTone(PlanStatus status) {
    switch (status) {
    case PlanStatus::Completed:
    case PlanStatus::RolledBack: return QStringLiteral("success");
    case PlanStatus::Failed: return QStringLiteral("danger");
    case PlanStatus::NeedsRecovery:
    case PlanStatus::Executing:
    case PlanStatus::RollingBack: return QStringLiteral("warning");
    default: return QStringLiteral("neutral");
    }
}

// 파일의 저장 상태를 번역 문구와 독립적인 표시 정보로 바꾸는 함수.
QString historyStatusTone(StepStatus status) {
    switch (status) {
    case StepStatus::Applied:
    case StepStatus::RolledBack: return QStringLiteral("success");
    case StepStatus::Failed: return QStringLiteral("danger");
    case StepStatus::NeedsRecovery: return QStringLiteral("warning");
    default: return QStringLiteral("neutral");
    }
}

} // namespace

// 기록 조회와 외부 소유 파일 실행 계약으로 기록 화면을 준비하는 생성자.
HistoryController::HistoryController(IHistoryRepository& history, IHistoryQuery& query, IFileOperations& operations, QObject* parent)
    : QObject(parent), history_(history), query_(query), operations_(operations) {}

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

// 검색 조건의 날짜를 지역 날짜 경계로 해석하고 첫 조회가 성공해야 필터를 확정하는 함수.
bool HistoryController::applyHistoryFilters(const QString& text, const QString& period, const QString& result,
    const QString& order, const QString& from, const QString& to) {
    if (runner_ && runner_->busy()) return false;
    HistorySearch search;
    search.text = text.trimmed().toStdString();
    // 잘못된 입력을 기존 결과와 함께 설명하는 함수.
    const auto invalid = [this](const QString& message) {
        searchMessage_ = message; emit historyChanged(); return false;
    };
    if (result == "all") search.result = HistoryResultFilter::All;
    else if (result == "completed") search.result = HistoryResultFilter::Completed;
    else if (result == "rolledBack") search.result = HistoryResultFilter::RolledBack;
    else if (result == "problem") search.result = HistoryResultFilter::Problem;
    else return invalid(QStringLiteral("작업 결과 필터를 확인해 주세요."));
    if (order == "newest") search.order = HistoryOrder::Newest;
    else if (order == "oldest") search.order = HistoryOrder::Oldest;
    else return invalid(QStringLiteral("정렬 순서를 확인해 주세요."));
    QDate start, end;
    const auto today = QDate::currentDate();
    if (period == "today" || period == "7days" || period == "30days") {
        start = today.addDays(period == "today" ? 0 : period == "7days" ? -6 : -29);
        end = today;
    } else if (period == "custom") {
        start = QDate::fromString(from, "yyyy-MM-dd");
        end = QDate::fromString(to, "yyyy-MM-dd");
        if (!start.isValid() || !end.isValid() || start.toString("yyyy-MM-dd") != from || end.toString("yyyy-MM-dd") != to || start > end)
            return invalid(QStringLiteral("시작일과 종료일을 YYYY-MM-DD 형식으로 입력해 주세요. 시작일은 종료일보다 늦을 수 없어요."));
    } else if (period != "all") return invalid(QStringLiteral("기간을 확인해 주세요."));
    if (start.isValid()) {
        const auto zone = QTimeZone::systemTimeZone();
        search.fromUtc = start.startOfDay(zone).toUTC().toString(Qt::ISODateWithMs).toStdString();
        search.untilUtc = end.addDays(1).startOfDay(zone).toUTC().toString(Qt::ISODateWithMs).toStdString();
    }
    if (!readSearchPage(search, false)) { emit historyChanged(); return false; }
    activeSearch_ = search;
    searchText_ = text.trimmed(); period_ = period; resultFilter_ = result; sortOrder_ = order;
    fromDate_ = from; toDate_ = to;
    emit filtersChanged();
    emit historyChanged();
    return true;
}

// 같은 필터를 유지하되 오늘 기준 기간과 첫 페이지를 새로 계산하는 함수.
void HistoryController::refreshHistory() {
    applyHistoryFilters(searchText_, period_, resultFilter_, sortOrder_, fromDate_, toDate_);
}
// 검색 조건을 초기화하고 첫 페이지를 다시 조회하는 함수.
void HistoryController::resetHistoryFilters() { applyHistoryFilters("", "all", "all", "newest", "", ""); }
// 현재 선택을 건드리지 않고 다음 커서 뒤의 기록을 추가하는 함수.
void HistoryController::loadMoreHistory() {
    if ((runner_ && runner_->busy()) || !nextCursor_) return;
    auto search = activeSearch_;
    search.after = nextCursor_;
    readSearchPage(search, true);
    emit historyChanged();
}
// 조회 실패 시 기존 선택과 목록을 지키고 성공하면 최대 한 페이지를 화면 값으로 바꾸는 함수.
bool HistoryController::readSearchPage(const HistorySearch& search, bool append) {
    const auto result = query_.searchHistory(search);
    if (!result) {
        searchMessage_ = QStringLiteral("작업 기록을 읽지 못했어요: %1").arg(QString::fromStdString(result.error().message));
        return false;
    }
    if (!append) { selectedHistoryPlan_.reset(); historySteps_.clear(); historyEntries_.clear(); }
    const auto today = QDate::currentDate();
    QString previousGroup = historyEntries_.isEmpty() ? QString() : historyEntries_.last().toMap().value("dateGroup").toString();
    QSet<QString> existing;
    for (const auto& row : historyEntries_) existing.insert(row.toMap().value("planId").toString());
    for (const auto& match : result.value().entries) {
        const auto& entry = match.plan;
        const auto id = QString::fromStdString(entry.id);
        if (existing.contains(id)) continue;
        existing.insert(id);
        const auto utc = QDateTime::fromString(QString::fromStdString(entry.lastActivityUtc), Qt::ISODateWithMs);
        QVariantMap row;
        const auto group = historyDateGroup(utc, today);
        row.insert("dateGroup", group);
        row.insert("startsDateGroup", group != previousGroup);
        previousGroup = group;
        row.insert("planId", id);
        row.insert("time", utc.isValid() ? utc.toLocalTime().toString("yyyy-MM-dd hh:mm") : QString::fromStdString(entry.lastActivityUtc));
        row.insert("status", historyStatusText(entry.status));
        row.insert("statusTone", historyStatusTone(entry.status));
        const auto local = utc.toLocalTime();
        row.insert("shortTime", utc.isValid() ? local.toString("hh:mm") : row.value("time").toString());
        const auto dateLabel = local.date() == today ? QStringLiteral("오늘")
            : local.date() == today.addDays(-1) ? QStringLiteral("어제") : local.toString("M월 d일");
        row.insert("detailTime", utc.isValid() ? dateLabel + " " + local.toString("hh:mm") : row.value("time").toString());
        row.insert("fileCount", entry.fileCount);
        row.insert("appliedCount", entry.appliedCount);
        row.insert("matchedFileCount", match.matchedFileCount);
        historyEntries_.append(row);
    }
    nextCursor_ = result.value().next;
    searchMessage_ = historyEntries_.isEmpty() ? QStringLiteral("조건에 맞는 작업 기록이 없어요.")
        : QStringLiteral("작업 %1건을 표시해요.%2").arg(historyEntries_.size()).arg(nextCursor_ ? QStringLiteral(" 더 불러올 수 있어요.") : QString());
    if (!append) historyMessage_ = QStringLiteral("작업을 선택하면 전체 파일과 일치한 파일을 확인할 수 있어요.");
    return true;
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
        const auto term = QString::fromStdString(activeSearch_.text).replace('\\', '/');
        row.insert(QStringLiteral("searchMatch"), !term.isEmpty() &&
            (displayPath(step.source).replace('\\', '/').contains(term, Qt::CaseInsensitive)
             || displayPath(step.destination).replace('\\', '/').contains(term, Qt::CaseInsensitive)));
        row.insert(QStringLiteral("status"), historyStepStatusText(step.status));
        row.insert(QStringLiteral("statusTone"), historyStatusTone(step.status));
        row.insert(QStringLiteral("applied"), step.status == StepStatus::Applied);
        row.insert(QStringLiteral("rolledBack"), step.status == StepStatus::RolledBack);
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
