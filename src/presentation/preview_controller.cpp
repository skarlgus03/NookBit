#include "presentation/preview_controller.hpp"

#include "infrastructure/configuration/session_settings_repository.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace nookbit {
namespace {

// Windows 경로를 Qt 문자열로 변환해 화면에 표시하는 함수.
QString displayPath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

} // namespace

// 화면 모델과 계획 작업 계약 및 보호할 앱 데이터 경로를 받는 생성자.
PreviewController::PreviewController(PendingFilesModel& pendingFiles,
                                     ICreatePreviewUseCase& createPreview,
                                     IFreezePreviewUseCase& freezePreview,
                                     IVerifySavedPlanUseCase& verifySavedPlan,
                                     IApprovePlanUseCase& approvePlan,
                                     IHistoryRepository& history,
                                     QString rulesPath,
                                     QString schemaPath,
                                     std::filesystem::path appDataRoot,
                                     QString defaultDestinationRoot,
                                     QObject* parent,
                                     QString legacySchemaPath)
    : QObject(parent),
      pendingFiles_(pendingFiles),
      createPreview_(createPreview),
      freezePreview_(freezePreview),
      verifySavedPlan_(verifySavedPlan),
      approvePlan_(approvePlan),
      rules_(std::move(rulesPath), std::move(schemaPath), std::move(legacySchemaPath)),
      planner_(ruleEngine_, appDataRoot),
      history_(history),
      appDataRoot_(std::move(appDataRoot)),
      defaultDestinationRoot_(std::move(defaultDestinationRoot)) {
    connect(&pendingFiles_, &QAbstractItemModel::rowsInserted, this, [this] { clear(); });
    connect(&pendingFiles_, &QAbstractItemModel::rowsRemoved, this, [this] { clear(); });
    refreshActiveRules();
}

// 화면에 표시할 파일별 이동 초안을 반환하는 함수.
QVariantList PreviewController::steps() const { return steps_; }

// 분류되지 않았거나 제외된 파일까지 포함한 미리보기 행을 반환하는 함수.
QVariantList PreviewController::outcomes() const { return outcomes_; }

// 화면에 표시할 검사 경고를 반환하는 함수.
QStringList PreviewController::warnings() const { return warnings_; }

// 최근 미리보기 결과 문구를 반환하는 함수.
QString PreviewController::message() const { return message_; }

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

// 처음 제안할 정리 목적지 루트를 반환하는 함수.
QString PreviewController::defaultDestinationRoot() const { return defaultDestinationRoot_; }

// 사용자 저장본을 화면에 표시할 짧은 규칙 식별자로 반환하는 함수.
QString PreviewController::activeRuleLabel() const { return activeRuleLabel_; }

// 저장된 규칙을 다시 읽고 이전 규칙으로 만든 미리보기를 무효화하는 함수.
void PreviewController::refreshActiveRules() {
    const auto loaded = rules_.load();
    const auto label = loaded
        ? QStringLiteral("%1 · %2").arg(QString::fromStdString(loaded.value().id),
                                          QString::fromStdString(loaded.value().version))
        : QStringLiteral("읽기 실패: %1").arg(QString::fromStdString(loaded.error().message));
    if (activeRuleLabel_ != label) {
        activeRuleLabel_ = label;
        emit activeRuleChanged();
    }
    clear();
}

// 대기 파일에 대해 실제 이동 없이 분류 미리보기를 만드는 함수.
void PreviewController::createPreview(const QString& destinationRoot) {
    refreshActiveRules();
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
    int plannedCount = 0;
    int skippedCount = 0;
    int unmatchedCount = 0;
    int excludedCount = 0;
    for (const auto& outcome : result.value().previewOutcomes) {
        QVariantMap row;
        row.insert(QStringLiteral("fileName"), displayPath(outcome.source.filename()));
        row.insert(QStringLiteral("source"), displayPath(outcome.source));
        row.insert(QStringLiteral("destination"), outcome.destination
            ? displayPath(*outcome.destination) : QString());
        const auto ruleId = QString::fromStdString(outcome.matchedRuleId);
        row.insert(QStringLiteral("rule"), ruleId);
        switch (outcome.disposition) {
        case PreviewDisposition::Planned:
            ++plannedCount;
            row.insert(QStringLiteral("status"), QStringLiteral("이동 예정"));
            row.insert(QStringLiteral("statusKey"), QStringLiteral("planned"));
            row.insert(QStringLiteral("reason"), QStringLiteral("규칙 '%1'에 일치했어요.").arg(ruleId));
            break;
        case PreviewDisposition::CollisionSkipped:
            ++skippedCount;
            row.insert(QStringLiteral("status"), QStringLiteral("충돌로 건너뜀"));
            row.insert(QStringLiteral("statusKey"), QStringLiteral("skipped"));
            row.insert(QStringLiteral("reason"), QStringLiteral("규칙 '%1'에 일치했지만 목적지에 같은 이름의 파일이 있어 이동하지 않아요.").arg(ruleId));
            break;
        case PreviewDisposition::Unmatched:
            ++unmatchedCount;
            row.insert(QStringLiteral("status"), QStringLiteral("미분류"));
            row.insert(QStringLiteral("statusKey"), QStringLiteral("unmatched"));
            row.insert(QStringLiteral("reason"), QStringLiteral("일치하는 규칙이 없어 대기 목록에 남아요."));
            break;
        case PreviewDisposition::Excluded:
            ++excludedCount;
            row.insert(QStringLiteral("status"), QStringLiteral("제외"));
            row.insert(QStringLiteral("statusKey"), QStringLiteral("excluded"));
            row.insert(QStringLiteral("reason"), QStringLiteral("규칙 '%1'에서 명시적으로 제외해 대기 목록에 남아요.").arg(ruleId));
            break;
        }
        outcomes_.append(row);
    }
    for (const auto& warning : result.value().warnings) {
        if (warning.code == "plan.unmatched_file" || warning.code == "plan.excluded_file") continue;
        warnings_.append(QString::fromStdString(warning.message));
    }
    hasPreview_ = true;
    message_ = QStringLiteral("이동 예정 %1 · 충돌 건너뜀 %2 · 미분류 %3 · 제외 %4 · 기타 경고 %5건. 실제 파일은 변경하지 않았어요.")
                   .arg(plannedCount).arg(skippedCount).arg(unmatchedCount)
                   .arg(excludedCount).arg(warnings_.size());
    emit previewChanged();
    emit destinationRootAccepted(previewHomeRoot_);
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
    UserSettings settings;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    std::vector<std::filesystem::path> selectedFiles;
    const auto paths = pendingFiles_.filePaths();
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());

    const auto result = verifySavedPlan_.execute(draft_->id, selectedFiles, settings);
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
    UserSettings settings;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    const auto paths = pendingFiles_.filePaths();
    std::vector<std::filesystem::path> selectedFiles;
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());
    const auto result = approvePlan_.execute(draft_->id, selectedFiles, settings);
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
    SafeExecutor executor(settings, verifySavedPlan_, scanner_, hasher_, history_, appDataRoot_);
    const auto result = executor.execute(loaded.value(), *approval_, selectedFiles);
    if (!result) {
        const auto current = history_.loadPlan(draft_->id);
        if (!current || current.value().status != PlanStatus::Approved) approval_.reset();
        emit recoveryReconcileRequested();
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

// 기록에서 되돌린 작업이 마지막 이동이면 버튼 상태를 갱신하는 함수.
void PreviewController::refreshLastUndoAfterHistoryRollback(const QString& planId) {
    if (!undoPlanId_ || *undoPlanId_ != planId.toStdString()) return;
    const auto current = history_.loadPlan(*undoPlanId_);
    if (!current || (current.value().status != PlanStatus::Completed &&
                     current.value().status != PlanStatus::Failed)) {
        undoPlanId_.reset();
        emit previewChanged();
    }
}

// 대기 목록이 바뀌었을 때 낡은 미리보기를 지우는 함수.
void PreviewController::clear() {
    if (steps_.isEmpty() && outcomes_.isEmpty() && warnings_.isEmpty() && message_.isEmpty() &&
        !hasPreview_ && !hasFrozenPlan_ && !draft_ && !approval_) return;
    steps_.clear();
    outcomes_.clear();
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
    outcomes_.clear();
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
