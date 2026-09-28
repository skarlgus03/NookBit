#include "presentation/preview_controller.hpp"

#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace nookbit {
namespace {

// 화면에서 지정한 목적지 루트를 이번 미리보기에만 제공하는 저장소.
class SessionSettingsRepository final : public ISettingsRepository {
public:
    /// 이번 미리보기의 목적지 루트를 보관하는 생성자.
    explicit SessionSettingsRepository(std::filesystem::path homeRoot) {
        settings_.homeRoot = std::move(homeRoot);
    }

    /// 저장된 목적지 루트와 기본 안전 정책을 반환하는 함수.
    [[nodiscard]] Result<UserSettings> load() const override {
        return Result<UserSettings>::success(settings_);
    }

    /// 화면 미리보기에서 설정 저장을 허용하지 않는 함수.
    [[nodiscard]] Result<void> save(const UserSettings&) override {
        return Result<void>::failure({"settings.read_only", "Preview settings are not persisted."});
    }

private:
    UserSettings settings_;
};

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

// 화면 모델과 미리보기 요청 계약 및 보호할 앱 데이터 경로를 받는 생성자.
PreviewController::PreviewController(PendingFilesModel& pendingFiles,
                                     ICreatePreviewUseCase& createPreview,
                                     IFreezePreviewUseCase& freezePreview,
                                     IHistoryRepository& history,
                                     QString rulesPath,
                                     QString schemaPath,
                                     std::filesystem::path appDataRoot,
                                     QString defaultDestinationRoot,
                                     QObject* parent)
    : QObject(parent),
      pendingFiles_(pendingFiles),
      createPreview_(createPreview),
      freezePreview_(freezePreview),
      rules_(std::move(rulesPath), std::move(schemaPath)),
      planner_(ruleEngine_, appDataRoot),
      history_(history),
      appDataRoot_(std::move(appDataRoot)),
      defaultDestinationRoot_(std::move(defaultDestinationRoot)) {
    connect(&pendingFiles_, &QAbstractItemModel::rowsInserted, this, [this] { clear(); });
    connect(&pendingFiles_, &QAbstractItemModel::rowsRemoved, this, [this] { clear(); });
}

// 화면에 표시할 파일별 이동 초안을 반환하는 함수.
QVariantList PreviewController::steps() const { return steps_; }

// 화면에 표시할 검사 경고를 반환하는 함수.
QStringList PreviewController::warnings() const { return warnings_; }

// 최근 미리보기 결과 문구를 반환하는 함수.
QString PreviewController::message() const { return message_; }

// 시작 시 발견한 미완료 작업 안내를 반환하는 함수.
QString PreviewController::recoveryNotice() const { return recoveryNotice_; }

// 사용자가 확인할 중단 작업 목록을 반환하는 함수.
QVariantList PreviewController::recoveryCases() const { return recoveryCases_; }

// 복구 화면의 마지막 검사 결과를 반환하는 함수.
QString PreviewController::recoveryMessage() const { return recoveryMessage_; }

// 시작 시 복구 검사 결과를 화면에 전달하는 함수.
void PreviewController::setRecoveryNotice(QString notice) {
    if (recoveryNotice_ == notice) return;
    recoveryNotice_ = std::move(notice);
    emit recoveryNoticeChanged();
}

// 중단 작업의 현재 원본·목적지 상태를 다시 읽는 함수.
void PreviewController::refreshRecoveryCases() {
    RecoveryService recovery(history_, scanner_, hasher_);
    auto inspected = recovery.inspectCases();
    if (!inspected) {
        recoveryMessage_ = QStringLiteral("복구 기록을 읽지 못했어요: %1")
                               .arg(QString::fromStdString(inspected.error().message));
        emit recoveryCasesChanged();
        return;
    }
    recoveryCases_.clear();
    QSet<QString> planIds;
    for (const auto& item : inspected.value()) {
        // 파일 검사 상태를 복구 화면의 한국어 문구로 바꾸는 함수.
        const auto stateText = [](FileProbe state) -> QString {
            switch (state) {
            case FileProbe::Missing: return QStringLiteral("없음");
            case FileProbe::Matches: return QStringLiteral("기록과 일치");
            case FileProbe::Different: return QStringLiteral("다른 파일");
            case FileProbe::Unavailable: return QStringLiteral("확인 불가");
            }
            return QStringLiteral("확인 불가");
        };
        QString detail;
        if (!item.contentHashAvailable) {
            detail = QStringLiteral("이전 기록에는 파일 내용 해시가 없어 자동 확정할 수 없어요.");
        } else if (!item.planIntact || !item.originKnown) {
            detail = QStringLiteral("계획 기록이나 중단 시점을 신뢰할 수 없어 자동 확정할 수 없어요.");
        } else if (item.canResolve) {
            detail = QStringLiteral("계획의 모든 파일 위치가 일치해, 파일을 옮기지 않고 기록만 정리할 수 있어요.");
        } else {
            detail = QStringLiteral("위치가 모호해요. 이 계획의 파일들을 직접 확인한 뒤 다시 검사해 주세요. 앱은 파일을 건드리지 않아요.");
        }
        QVariantMap row;
        const auto planId = QString::fromStdString(item.planId);
        planIds.insert(planId);
        row.insert(QStringLiteral("planId"), planId);
        row.insert(QStringLiteral("partIndex"), static_cast<int>(item.partIndex));
        row.insert(QStringLiteral("partCount"), static_cast<int>(item.partCount));
        row.insert(QStringLiteral("source"), displayPath(item.source));
        row.insert(QStringLiteral("destination"), displayPath(item.destination));
        row.insert(QStringLiteral("sourceState"), stateText(item.sourceState));
        row.insert(QStringLiteral("destinationState"), stateText(item.destinationState));
        row.insert(QStringLiteral("canResolve"), item.canResolve);
        row.insert(QStringLiteral("detail"), detail);
        recoveryCases_.append(row);
    }
    recoveryMessage_ = QStringLiteral("현재 파일 상태를 다시 확인했어요. 파일은 이동하지 않았어요.");
    setRecoveryNotice(recoveryCases_.isEmpty()
        ? QString() : QStringLiteral("확인이 필요한 중단 작업 %1건이 있어요. 파일은 건드리지 않았어요.")
                          .arg(planIds.size()));
    emit recoveryCasesChanged();
}

// 파일 위치를 다시 검증하고 확실한 작업 기록만 정리하는 함수.
void PreviewController::resolveRecoveryCase(const QString& planId) {
    RecoveryService recovery(history_, scanner_, hasher_);
    auto resolved = recovery.resolveCase(planId.toStdString());
    if (!resolved) {
        recoveryMessage_ = QStringLiteral("기록을 정리하지 못했어요: %1")
                               .arg(QString::fromStdString(resolved.error().message));
        emit recoveryCasesChanged();
        return;
    }
    refreshRecoveryCases();
    recoveryMessage_ = QStringLiteral("파일을 옮기지 않고 현재 상태에 맞게 작업 기록을 정리했어요.");
    emit recoveryCasesChanged();
}

// 유효한 미리보기 초안의 존재 여부를 반환하는 함수.
bool PreviewController::hasPreview() const noexcept { return hasPreview_; }

// 현재 미리보기를 SQLite에 고정 저장했는지 반환하는 함수.
bool PreviewController::hasFrozenPlan() const noexcept { return hasFrozenPlan_; }

bool PreviewController::hasApprovedPlan() const noexcept { return approval_.has_value(); }

// 현재 계획에 실제 이동할 파일이 있는지 반환하는 함수.
bool PreviewController::hasMovableSteps() const noexcept {
    return draft_ && std::any_of(draft_->steps.begin(), draft_->steps.end(),
        [](const MoveStep& step) { return step.status == StepStatus::Planned; });
}

bool PreviewController::canUndo() const noexcept { return undoPlanId_.has_value(); }

// 최근 실행 작업 목록을 화면에 반환하는 함수.
QVariantList PreviewController::historyEntries() const { return historyEntries_; }

// 선택한 작업의 파일별 이동 기록을 반환하는 함수.
QVariantList PreviewController::historySteps() const { return historySteps_; }

// 기록 화면의 안내 문구를 반환하는 함수.
QString PreviewController::historyMessage() const { return historyMessage_; }

// 선택한 작업 ID를 반환하는 함수.
QString PreviewController::selectedHistoryPlanId() const {
    return selectedHistoryPlan_ ? QString::fromStdString(selectedHistoryPlan_->id) : QString();
}

// 선택한 작업에 되돌릴 파일이 있는지 반환하는 함수.
bool PreviewController::canUndoSelectedHistory() const noexcept {
    if (!selectedHistoryPlan_ ||
        (selectedHistoryPlan_->status != PlanStatus::Completed &&
         selectedHistoryPlan_->status != PlanStatus::Failed)) return false;
    return std::any_of(selectedHistoryPlan_->steps.begin(), selectedHistoryPlan_->steps.end(),
        [](const MoveStep& step) { return step.status == StepStatus::Applied; });
}

// 선택한 작업에서 되돌릴 파일 수를 반환하는 함수.
int PreviewController::selectedHistoryFileCount() const noexcept {
    if (!selectedHistoryPlan_) return 0;
    return static_cast<int>(std::count_if(
        selectedHistoryPlan_->steps.begin(), selectedHistoryPlan_->steps.end(),
        [](const MoveStep& step) { return step.status == StepStatus::Applied; }));
}

// 처음 제안할 정리 목적지 루트를 반환하는 함수.
QString PreviewController::defaultDestinationRoot() const { return defaultDestinationRoot_; }

// 대기 파일에 대해 실제 이동 없이 분류 미리보기를 만드는 함수.
void PreviewController::createPreview(const QString& destinationRoot) {
    clear();
    if (pendingFiles_.count() == 0) {
        showError(QStringLiteral("먼저 파일을 대기 목록에 추가해 주세요."));
        return;
    }
    const QFileInfo homeInfo(destinationRoot.trimmed());
    if (!homeInfo.isAbsolute() || QDir(homeInfo.absoluteFilePath()).isRoot()) {
        showError(QStringLiteral("정리할 위치는 드라이브 루트가 아닌 절대 경로로 지정해 주세요."));
        return;
    }

    CreatePreviewRequest request;
    request.destinationRoot = std::filesystem::path(homeInfo.absoluteFilePath().toStdWString());
    const auto paths = pendingFiles_.filePaths();
    request.selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) {
        request.selectedFiles.emplace_back(path.toStdWString());
    }

    const auto result = createPreview_.execute(request);
    if (!result) {
        showError(QStringLiteral("미리보기를 만들지 못했어요: %1")
                      .arg(QString::fromStdString(result.error().message)));
        return;
    }
    draft_ = result.value();
    previewHomeRoot_ = homeInfo.absoluteFilePath();

    for (const auto& step : result.value().steps) {
        QVariantMap row;
        row.insert(QStringLiteral("fileName"), displayPath(step.source.filename()));
        row.insert(QStringLiteral("source"), displayPath(step.source));
        row.insert(QStringLiteral("destination"), displayPath(step.destination));
        row.insert(QStringLiteral("rule"), QString::fromStdString(step.matchedRuleId));
        row.insert(QStringLiteral("status"), step.status == StepStatus::Skipped
                                                  ? QStringLiteral("건너뜀")
                                                  : QStringLiteral("예정"));
        steps_.append(row);
    }
    for (const auto& warning : result.value().warnings) {
        warnings_.append(QString::fromStdString(warning.message));
    }
    hasPreview_ = true;
    message_ = QStringLiteral("미리보기 %1건, 경고 %2건. 실제 파일은 변경하지 않았어요.")
                   .arg(steps_.size()).arg(warnings_.size());
    emit previewChanged();
}

// 현재 미리보기를 재검사한 뒤 승인 전 계획으로 고정·저장하는 함수.
void PreviewController::freezePreview() {
    if (!draft_ || !hasPreview_ || hasFrozenPlan_) {
        showError(QStringLiteral("먼저 새 미리보기를 만들어 주세요."));
        return;
    }
    UserSettings settings;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    std::vector<std::filesystem::path> selectedFiles;
    const auto paths = pendingFiles_.filePaths();
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());

    const auto result = freezePreview_.execute(*draft_, selectedFiles, settings);
    if (!result) {
        showError(QStringLiteral("계획을 저장하지 못했어요. 새 미리보기를 확인해 주세요: %1")
                      .arg(QString::fromStdString(result.error().message)));
        return;
    }
    draft_ = result.value();
    hasFrozenPlan_ = true;
    message_ = QStringLiteral("계획을 안전하게 저장했어요. 아직 승인하거나 파일을 이동하지 않았어요.");
    emit previewChanged();
}

// 저장된 계획을 DB에서 다시 읽고 현재 상태와 비교하는 함수.
bool PreviewController::verifySavedPlan() {
    if (!draft_ || !hasFrozenPlan_) {
        message_ = QStringLiteral("먼저 계획을 고정·저장해 주세요.");
        emit previewChanged();
        return false;
    }
    SessionSettingsRepository settings(
        std::filesystem::path(previewHomeRoot_.toStdWString()));
    std::vector<std::filesystem::path> selectedFiles;
    const auto paths = pendingFiles_.filePaths();
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());

    VerifySavedPlanUseCase useCase(settings, rules_, scanner_, planner_, hasher_, history_);
    const auto result = useCase.execute(draft_->id, selectedFiles);
    if (!result) {
        message_ = QStringLiteral("저장 계획 검사 실패. 새 미리보기가 필요할 수 있어요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit previewChanged();
        return false;
    }
    message_ = QStringLiteral("검사 시점에는 저장 계획과 현재 상태가 일치해요. 아직 승인·이동하지 않았어요.");
    emit previewChanged();
    return true;
}

// 저장된 파일 계획을 다시 검사한 뒤 승인 기록을 저장하는 함수.
void PreviewController::approvePlan() {
    if (!draft_ || !hasFrozenPlan_ || approval_ || pendingFiles_.count() == 0 ||
        !hasMovableSteps()) {
        message_ = QStringLiteral("승인하려면 이동 예정인 파일의 계획을 고정·저장해 주세요.");
        emit previewChanged();
        return;
    }
    SessionSettingsRepository settings(std::filesystem::path(previewHomeRoot_.toStdWString()));
    VerifySavedPlanUseCase verifier(settings, rules_, scanner_, planner_, hasher_, history_);
    ApprovePlanUseCase approver(verifier, history_);
    const auto paths = pendingFiles_.filePaths();
    std::vector<std::filesystem::path> selectedFiles;
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());
    const auto result = approver.execute(draft_->id, selectedFiles);
    if (!result) {
        message_ = QStringLiteral("승인하지 못했어요. 새 미리보기가 필요할 수 있어요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit previewChanged();
        return;
    }
    approval_ = result.value();
    message_ = QStringLiteral("파일 이동을 승인했어요. 실제 이동은 '승인한 파일들 이동'을 눌러야 시작돼요.");
    emit previewChanged();
}

// 승인된 파일들을 다시 검사한 뒤 순서대로 이동하는 함수.
void PreviewController::moveApprovedFile() {
    if (!draft_ || !approval_ || pendingFiles_.count() == 0 || previewHomeRoot_.isEmpty()) {
        message_ = QStringLiteral("먼저 파일 이동 계획을 승인해 주세요.");
        emit previewChanged();
        return;
    }
    const auto paths = pendingFiles_.filePaths();
    std::vector<std::filesystem::path> selectedFiles;
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());
    const auto loaded = history_.loadPlan(draft_->id);
    if (!loaded || loaded.value().status != PlanStatus::Approved) {
        approval_.reset();
        message_ = QStringLiteral("승인된 계획을 읽지 못했어요. 새 미리보기부터 다시 시작해 주세요.");
        emit previewChanged();
        return;
    }
    SessionSettingsRepository settings(std::filesystem::path(previewHomeRoot_.toStdWString()));
    VerifySavedPlanUseCase verifier(settings, rules_, scanner_, planner_, hasher_, history_);
    SafeExecutor executor(settings, verifier, scanner_, hasher_, history_, appDataRoot_);
    const auto result = executor.execute(loaded.value(), *approval_, selectedFiles);
    if (!result) {
        const auto current = history_.loadPlan(draft_->id);
        if (!current || current.value().status != PlanStatus::Approved) approval_.reset();
        RecoveryService recovery(history_, scanner_, hasher_);
        (void)recovery.reconcile();
        refreshRecoveryCases();
        message_ = QStringLiteral("이동을 완료하지 못했어요. 파일 위치와 작업 기록을 확인해 주세요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit previewChanged();
        return;
    }
    const auto movedPlanId = draft_->id;
    const auto movedSteps = loaded.value().steps;
    for (int index = pendingFiles_.count() - 1; index >= 0; --index) {
        const auto path = std::filesystem::path(pendingFiles_.filePaths().at(index).toStdWString());
        if (std::any_of(movedSteps.begin(), movedSteps.end(), [&path](const MoveStep& step) {
                return step.status == StepStatus::Planned && step.source == path;
            })) {
            pendingFiles_.removeAt(index);
        }
    }
    undoPlanId_ = movedPlanId;
    message_ = QStringLiteral("파일 %1개를 이동했어요. '마지막 이동 되돌리기'로 원래 위치에 돌려놓을 수 있어요.")
                   .arg(std::count_if(movedSteps.begin(), movedSteps.end(), [](const MoveStep& step) {
                       return step.status == StepStatus::Planned;
                   }));
    emit previewChanged();
}

// 마지막으로 이동한 계획의 파일들을 안전하게 되돌리는 함수.
void PreviewController::undoLastMove() {
    if (!undoPlanId_) {
        message_ = QStringLiteral("되돌릴 이동 작업이 없어요.");
        emit previewChanged();
        return;
    }
    SessionSettingsRepository settings(std::filesystem::path(defaultDestinationRoot_.toStdWString()));
    SafeExecutor executor(settings, scanner_, hasher_, history_, appDataRoot_);
    const auto result = executor.rollback(*undoPlanId_);
    if (!result) {
        const auto current = history_.loadPlan(*undoPlanId_);
        if (current && current.value().status != PlanStatus::Completed &&
            current.value().status != PlanStatus::Failed) {
            undoPlanId_.reset();
        }
        message_ = QStringLiteral("되돌리지 못했어요. 파일 위치와 작업 기록을 확인해 주세요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit previewChanged();
        return;
    }
    undoPlanId_.reset();
    message_ = QStringLiteral("파일을 원래 위치로 되돌렸어요.");
    emit previewChanged();
}

// 최근 실행 작업 목록을 SQLite에서 다시 읽는 함수.
void PreviewController::refreshHistory() {
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
void PreviewController::selectHistoryPlan(const QString& planId) {
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
        historySteps_.append(row);
    }
    historyMessage_ = canUndoSelectedHistory()
        ? QStringLiteral("원본과 목적지를 확인한 뒤 되돌리기를 선택해 주세요. 파일이 바뀌었으면 안전 검사가 중단해요.")
        : QStringLiteral("이 작업은 현재 기록 상태에서 되돌릴 이동 파일이 없어요.");
    emit historyChanged();
}

// 선택한 과거 작업의 파일을 재검증하고 안전한 되돌리기를 실행하는 함수.
void PreviewController::undoSelectedHistory() {
    if (!canUndoSelectedHistory()) {
        historyMessage_ = QStringLiteral("되돌릴 수 있는 작업을 먼저 선택해 주세요.");
        emit historyChanged();
        return;
    }
    const auto planId = selectedHistoryPlan_->id;
    SessionSettingsRepository settings(std::filesystem::path(defaultDestinationRoot_.toStdWString()));
    SafeExecutor executor(settings, scanner_, hasher_, history_, appDataRoot_);
    const auto result = executor.rollback(planId);
    if (undoPlanId_ && *undoPlanId_ == planId) {
        const auto current = history_.loadPlan(planId);
        if (result || !current || (current.value().status != PlanStatus::Completed &&
                                    current.value().status != PlanStatus::Failed)) {
            undoPlanId_.reset();
            emit previewChanged();
        }
    }
    refreshHistory();
    if (!result) {
        const auto current = history_.loadPlan(planId);
        if (current && current.value().status == PlanStatus::NeedsRecovery) {
            refreshRecoveryCases();
        }
        historyMessage_ = QStringLiteral("되돌리지 못했어요. 파일은 덮어쓰지 않았어요: %1")
                              .arg(QString::fromStdString(result.error().message));
    } else {
        historyMessage_ = QStringLiteral("선택한 작업의 파일을 원래 위치로 되돌렸어요.");
    }
    emit historyChanged();
}

// 대기 목록이 바뀌었을 때 낡은 미리보기를 지우는 함수.
void PreviewController::clear() {
    if (steps_.isEmpty() && warnings_.isEmpty() && message_.isEmpty() &&
        !hasPreview_ && !hasFrozenPlan_ && !draft_ && !approval_) return;
    steps_.clear();
    warnings_.clear();
    message_.clear();
    hasPreview_ = false;
    hasFrozenPlan_ = false;
    draft_.reset();
    approval_.reset();
    previewHomeRoot_.clear();
    emit previewChanged();
}

// 실패 이유를 화면에 알리고 기존 초안을 지우는 함수.
void PreviewController::showError(const QString& message) {
    steps_.clear();
    warnings_.clear();
    hasPreview_ = false;
    hasFrozenPlan_ = false;
    draft_.reset();
    approval_.reset();
    previewHomeRoot_.clear();
    message_ = message;
    emit previewChanged();
}

} // namespace nookbit
