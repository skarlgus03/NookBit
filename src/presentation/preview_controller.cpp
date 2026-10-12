#include "presentation/preview_controller.hpp"


#include <QDir>
#include <QFileInfo>
#include <QVariantMap>
#include <QElapsedTimer>

#include <algorithm>
#include <utility>
#include <memory>

namespace nookbit {
namespace {

// Windows 경로를 Qt 문자열로 변환해 화면에 표시하는 함수.
QString displayPath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

} // namespace

// 화면 모델·계획 작업·파일 실행 계약과 기본 목적지를 받는 생성자.
PreviewController::PreviewController(PendingFilesModel& pendingFiles,
                                     ICreatePreviewUseCase& createPreview,
                                     IFreezePreviewUseCase& freezePreview,
                                     IVerifySavedPlanUseCase& verifySavedPlan,
                                     IApprovePlanUseCase& approvePlan,
                                     IHistoryRepository& history,
                                     IFileOperations& operations,
                                     QString rulesPath,
                                     QString schemaPath,
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
      history_(history),
      operations_(operations),
      defaultDestinationRoot_(std::move(defaultDestinationRoot)) {
    connect(&pendingFiles_, &QAbstractItemModel::rowsInserted, this, [this] { clear(); });
    connect(&pendingFiles_, &QAbstractItemModel::rowsRemoved, this, [this] { clear(); });
    connect(&pendingFiles_, &QAbstractItemModel::modelReset, this, [this] { clear(); });
    connect(&pendingFiles_, &QAbstractItemModel::dataChanged, this, [this] { clear(); });
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

// 규칙이 기본값을 상속할 때 적용할 충돌 정책의 저장 키를 반환하는 함수.
QString PreviewController::collisionPolicy() const {
    switch (collisionPolicy_) {
    case CollisionPolicy::Skip: return QStringLiteral("skip");
    case CollisionPolicy::Abort: return QStringLiteral("abort");
    default: return QStringLiteral("rename");
    }
}

// 유효한 정책만 적용하고 낡은 미리보기·승인을 무효화하는 함수.
void PreviewController::setCollisionPolicy(const QString& policy) {
    if (runner_ && runner_->busy()) return;
    CollisionPolicy next;
    if (policy == QStringLiteral("rename")) next = CollisionPolicy::Rename;
    else if (policy == QStringLiteral("skip")) next = CollisionPolicy::Skip;
    else if (policy == QStringLiteral("abort")) next = CollisionPolicy::Abort;
    else return;
    if (collisionPolicy_ == next) return;
    collisionPolicy_ = next;
    clear();
    emit collisionPolicyChanged();
}

// 저장된 규칙을 다시 읽고 이전 규칙으로 만든 미리보기를 무효화하는 함수.
void PreviewController::refreshActiveRules() {
    if (runner_ && runner_->busy()) return;
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
    if (runner_ && runner_->busy()) return;
    refreshActiveRules();
    pendingFiles_.refreshAvailability();
    if (pendingFiles_.unavailableCount() > 0) {
        showError(QStringLiteral("찾을 수 없는 파일 %1개가 있어요. 대기 목록에서 해당 항목을 빼고 다시 미리보기를 만들어 주세요.")
                      .arg(pendingFiles_.unavailableCount()));
        return;
    }
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
    request.defaultCollisionPolicy = collisionPolicy_;
    request.allowedDestinationRoots = allowedDestinationRoots_;
    const auto paths = pendingFiles_.filePaths();
    request.selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) {
        request.selectedFiles.emplace_back(path.toStdWString());
    }

    const auto result = createPreview_.execute(request);
    if (!result) {
        if (result.error().code == "path.destination_outside_home") {
            showError(QStringLiteral("외부 목적지가 아직 허용되지 않았어요. 규칙 편집기의 목적지 설정에서 해당 폴더를 허용해 주세요."));
            return;
        }
        if (result.error().code == "collision.abort") {
            showError(QStringLiteral("목적지 파일 이름이 충돌해 '작업 중단' 정책에 따라 미리보기를 중단했어요. 파일은 이동하지 않았어요."));
            return;
        }
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

// 사용자의 최종 확인을 기다리는지 반환하는 함수.
bool PreviewController::awaitingConfirmation() const noexcept { return awaitingConfirmation_; }

// 고정된 계획에서 실제 이동할 원본·목적지만 반환하는 함수.
QVariantList PreviewController::confirmationSteps() const {
    QVariantList rows;
    if (!draft_ || !hasFrozenPlan_) return rows;
    for (const auto& step : draft_->steps) {
        if (step.status != StepStatus::Planned) continue;
        QVariantMap row;
        row.insert(QStringLiteral("source"), displayPath(step.source));
        row.insert(QStringLiteral("destination"), displayPath(step.destination));
        rows.push_back(row);
    }
    return rows;
}

// 계획을 고정·검사하고 실제 이동 없이 최종 확인을 준비하는 함수.
bool PreviewController::prepareSort() {
    if (runner_ && runner_->busy()) return false;
    awaitingConfirmation_ = false;
    if (!hasPreview_ || !hasMovableSteps() || approval_) {
        message_ = QStringLiteral("먼저 이동 예정인 파일의 미리보기를 만들어 주세요.");
        emit previewChanged();
        return false;
    }
    if (runner_ && sortWorkflow_) { prepareSortInBackground(); return false; }
    if (!hasFrozenPlan_) freezePreview();
    if (!hasFrozenPlan_) return false;
    if (!verifySavedPlan()) {
        showError(message_);
        return false;
    }
    awaitingConfirmation_ = true;
    message_ = QStringLiteral("이동할 파일과 목적지를 최종 확인해 주세요. 아직 파일은 이동하지 않았어요.");
    emit previewChanged();
    return true;
}

// 준비된 최종 확인을 취소하고 파일을 그대로 두는 함수.
void PreviewController::cancelSort() {
    if (runner_ && runner_->busy()) return;
    if (!awaitingConfirmation_) return;
    awaitingConfirmation_ = false;
    message_ = QStringLiteral("정리를 취소했어요. 파일은 그대로 있어요.");
    emit previewChanged();
}

// 사용자가 확인한 고정 계획만 다시 검사·승인하고 실행하는 함수.
void PreviewController::confirmSort() {
    if (runner_ && runner_->busy()) return;
    if (!awaitingConfirmation_) return;
    awaitingConfirmation_ = false;
    if (runner_ && sortWorkflow_) { confirmSortInBackground(); return; }
    approvePlan();
    if (!approval_) {
        showError(message_);
        return;
    }
    moveApprovedFile();
    if (!runner_ && hasPreview_) showError(message_);
}

// 파일 검사·해시 계산·저장을 작업 스레드로 보내 확인 결과만 화면에 반영하는 함수.
void PreviewController::prepareSortInBackground() {
    UserSettings settings;
    settings.allowedDestinationRoots = allowedDestinationRoots_;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    settings.defaultCollisionPolicy = collisionPolicy_;
    std::vector<std::filesystem::path> selected;
    const auto paths = pendingFiles_.filePaths();
    for (const auto& path : paths) selected.emplace_back(path.toStdWString());
    auto prepared = std::make_shared<Result<OperationPlan>>(Result<OperationPlan>::failure({"sort.not_prepared", "Preparation did not complete."}));
    auto elapsedMs = std::make_shared<qint64>(-1);
    message_ = QStringLiteral("정리할 파일을 검사하고 있어요. 아직 이동하지 않았어요.");
    emit previewChanged();
    dispatchOperation(runner_, this,
        [workflow = sortWorkflow_, plan = *draft_, frozen = hasFrozenPlan_, selected, settings, prepared, elapsedMs] {
            QElapsedTimer timer; timer.start();
            *prepared = workflow->prepare(plan, frozen, selected, settings);
            *elapsedMs = timer.elapsed();
            return *prepared ? Result<void>::success() : Result<void>::failure(prepared->error());
        }, [this, paths, prepared, elapsedMs](const Result<void>& result) {
            if (!result) {
                auto error = QStringLiteral("정리 준비 검사에 실패했어요: %1").arg(QString::fromStdString(result.error().message));
                if (*elapsedMs >= 0) error += QStringLiteral(" (검사 %1초)").arg(static_cast<double>(*elapsedMs) / 1000.0, 0, 'f', 2);
                showError(error); return;
            }
            if (pendingFiles_.filePaths() != paths) { showError(QStringLiteral("검사 중 대기 목록이 바뀌었어요. 새 미리보기를 만들어 주세요.")); return; }
            draft_ = prepared->value(); hasFrozenPlan_ = true; awaitingConfirmation_ = true;
            message_ = QStringLiteral("이동할 파일과 목적지를 최종 확인해 주세요. 아직 파일은 이동하지 않았어요.");
            message_ += QStringLiteral(" (검사 %1초)").arg(static_cast<double>(*elapsedMs) / 1000.0, 0, 'f', 2);
            emit previewChanged();
        });
}

// 명시적 확인 뒤 승인 재검사와 실제 이동을 한 작업 스레드에서 이어 실행하는 함수.
void PreviewController::confirmSortInBackground() {
    if (!draft_ || !hasFrozenPlan_) return;
    UserSettings settings;
    settings.allowedDestinationRoots = allowedDestinationRoots_;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    settings.defaultCollisionPolicy = collisionPolicy_;
    std::vector<std::filesystem::path> selected;
    for (const auto& path : pendingFiles_.filePaths()) selected.emplace_back(path.toStdWString());
    const auto plan = *draft_;
    auto elapsedMs = std::make_shared<qint64>(-1);
    message_ = QStringLiteral("승인 내용을 다시 검사한 뒤 정리하고 있어요."); emit previewChanged();
    emit sortExecutionStarted();
    dispatchOperation(runner_, this,
        [workflow = sortWorkflow_, plan, selected, settings, elapsedMs] {
            QElapsedTimer timer; timer.start();
            const auto result = workflow->approveAndExecute(plan, selected, settings);
            *elapsedMs = timer.elapsed();
            return result;
        },
        [this, plan, elapsedMs](const Result<void>& result) {
            finishMovement(plan.id, plan.steps, result);
            if (*elapsedMs >= 0) {
                message_ += QStringLiteral(" (검사·이동 총 %1초)").arg(static_cast<double>(*elapsedMs) / 1000.0, 0, 'f', 2);
                emit previewChanged();
            }
        }, BackgroundTaskRunner::TaskKind::FileWork);
}

// 현재 미리보기를 재검사한 뒤 승인 전 계획으로 고정·저장하는 함수.
void PreviewController::freezePreview() {
    if (runner_ && runner_->busy()) return;
    if (!draft_ || !hasPreview_ || hasFrozenPlan_) {
        showError(QStringLiteral("먼저 새 미리보기를 만들어 주세요."));
        return;
    }
    UserSettings settings;
    settings.allowedDestinationRoots = allowedDestinationRoots_;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    settings.defaultCollisionPolicy = collisionPolicy_;
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
    if (runner_ && runner_->busy()) return false;
    if (!draft_ || !hasFrozenPlan_) {
        message_ = QStringLiteral("먼저 계획을 고정·저장해 주세요.");
        emit previewChanged();
        return false;
    }
    UserSettings settings;
    settings.allowedDestinationRoots = allowedDestinationRoots_;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    settings.defaultCollisionPolicy = collisionPolicy_;
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
    if (runner_ && runner_->busy()) return;
    if (!draft_ || !hasFrozenPlan_ || approval_ || pendingFiles_.count() == 0 ||
        !hasMovableSteps()) {
        message_ = QStringLiteral("승인하려면 이동 예정인 파일의 계획을 고정·저장해 주세요.");
        emit previewChanged();
        return;
    }
    UserSettings settings;
    settings.allowedDestinationRoots = allowedDestinationRoots_;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    settings.defaultCollisionPolicy = collisionPolicy_;
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
    if (runner_ && runner_->busy()) return;
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
    UserSettings settings;
    settings.allowedDestinationRoots = allowedDestinationRoots_;
    settings.homeRoot = std::filesystem::path(previewHomeRoot_.toStdWString());
    settings.defaultCollisionPolicy = collisionPolicy_;
    const auto movedPlanId = draft_->id;
    const auto movedSteps = loaded.value().steps;
    message_ = QStringLiteral("파일을 정리하고 있어요. 작업이 끝날 때까지 앱을 닫지 마세요.");
    emit previewChanged();
    emit sortExecutionStarted();
    dispatchOperation(runner_, this,
        [operations = &operations_, plan = loaded.value(), approval = *approval_, selectedFiles, settings] {
            return operations->executeApproved(plan, approval, selectedFiles, settings);
        }, [this, movedPlanId, movedSteps](const Result<void>& result) {
        finishMovement(movedPlanId, movedSteps, result);
      }, BackgroundTaskRunner::TaskKind::FileWork);
}

// 이동 완료/실패를 화면 스레드에서 동일한 규칙으로 처리하는 함수.
void PreviewController::finishMovement(const PlanId& movedPlanId, const std::vector<MoveStep>& movedSteps, const Result<void>& result) {
    if (!result) {
        const auto current = history_.loadPlan(movedPlanId);
        if (current && current.value().status == PlanStatus::Failed) {
            const auto& completed = current.value().steps;
            for (int index = pendingFiles_.count() - 1; index >= 0; --index) {
                const auto path = std::filesystem::path(pendingFiles_.filePaths().at(index).toStdWString());
                if (std::any_of(completed.begin(), completed.end(), [&](const MoveStep& step) {
                    return step.status == StepStatus::Applied && step.source == path;
                })) pendingFiles_.removeAt(index);
            }
            if (std::any_of(completed.begin(), completed.end(), [](const MoveStep& step) { return step.status == StepStatus::Applied; })) undoPlanId_ = movedPlanId;
        }
        if (!current || current.value().status != PlanStatus::Approved) approval_.reset();
        emit recoveryReconcileRequested();
        message_ = QStringLiteral("이동을 완료하지 못했어요. 파일 위치와 작업 기록을 확인해 주세요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit previewChanged();
        emit sortExecutionFinished(false, message_);
        return;
    }
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
    emit sortExecutionFinished(true, message_);
}

// 마지막으로 이동한 계획의 파일들을 안전하게 되돌리는 함수.
void PreviewController::undoLastMove() {
    if (runner_ && runner_->busy()) return;
    if (!undoPlanId_) {
        message_ = QStringLiteral("되돌릴 이동 작업이 없어요.");
        emit previewChanged();
        return;
    }
    const auto id = *undoPlanId_;
    dispatchOperation(runner_, this, [operations = &operations_, id] { return operations->rollback(id); },
        [this, id](const Result<void>& result) {
        if (!result) {
            const auto current = history_.loadPlan(id);
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
    }, BackgroundTaskRunner::TaskKind::FileWork);
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
    if (runner_ && runner_->busy()) return;
    if (steps_.isEmpty() && outcomes_.isEmpty() && warnings_.isEmpty() && message_.isEmpty() &&
        !hasPreview_ && !hasFrozenPlan_ && !draft_ && !approval_) return;
    steps_.clear();
    outcomes_.clear();
    warnings_.clear();
    message_.clear();
    hasPreview_ = false;
    hasFrozenPlan_ = false;
    awaitingConfirmation_ = false;
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
    awaitingConfirmation_ = false;
    draft_.reset();
    approval_.reset();
    previewHomeRoot_.clear();
    message_ = message;
    emit previewChanged();
}

} // namespace nookbit
