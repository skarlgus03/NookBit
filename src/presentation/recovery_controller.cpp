#include "presentation/recovery_controller.hpp"

#include "infrastructure/filesystem/recovery_service.hpp"

#include <QSet>
#include <QVariantMap>

#include <utility>
#include <algorithm>

namespace nookbit {
namespace {

// Windows 경로를 Qt 문자열로 변환해 화면에 표시하는 함수.
QString displayPath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

// 파일 검사 상태를 복구 화면의 한국어 문구로 바꾸는 함수.
QString stateText(FileProbe state) {
    switch (state) {
    case FileProbe::Missing: return QStringLiteral("없음");
    case FileProbe::Matches: return QStringLiteral("기록과 일치");
    case FileProbe::Different: return QStringLiteral("다른 파일");
    case FileProbe::Unavailable: return QStringLiteral("확인 불가");
    }
    return QStringLiteral("확인 불가");
}
// 주요 차단 이유를 사용자 안내로 바꾸고 모르는 오류는 원문을 보존하는 함수.
QString recoveryError(const Error& error) {
    const auto code = QString::fromStdString(error.code);
    if (code == "recovery.configuration_changed" || code == "recovery.settings_changed")
        return QStringLiteral("현재 정리 위치·충돌 정책·규칙이 원래 승인과 달라요. 이전 설정과 규칙을 복원한 뒤 다시 검사해 주세요.");
    if (code == "recovery.choice_stale") return QStringLiteral("표시 당시의 승인 기록이 바뀌었어요. 다시 검사하고 선택해 주세요.");
    if (code == "recovery.not_interrupted") return QStringLiteral("이동 처리는 이미 끝났어요. 보존된 임시 파일은 별도 정리가 필요해요.");
    if (code == "recovery.preserve_unsupported" || code == "recovery.step_resolution_required" || code == "recovery.transfer_resume_unsupported")
        return QStringLiteral("현재 중단 경계는 이 선택으로 처리할 수 없어요. 파일과 기록은 보존합니다.");
    return QStringLiteral("%1 (%2)").arg(QString::fromStdString(error.message), code);
}
// 전체 복구 진단의 파일별 상태를 짧은 화면 문구로 바꾸는 함수.
QString recoveryStepText(InterruptedStepState state) {
    switch (state) {
    case InterruptedStepState::Pending: return QStringLiteral("미시작");
    case InterruptedStepState::Moved: return QStringLiteral("이동 완료");
    case InterruptedStepState::Restored: return QStringLiteral("복원 완료");
    case InterruptedStepState::Skipped: return QStringLiteral("제외");
    case InterruptedStepState::Preserved: return QStringLiteral("부분 보존");
    case InterruptedStepState::NeedsAttention: return QStringLiteral("확인 필요");
    }
    return QStringLiteral("확인 필요");
}

} // namespace

// 복구 기록 저장소를 받아 화면의 복구 작업을 준비하는 생성자.
RecoveryController::RecoveryController(IHistoryRepository& history, QObject* parent, const ITransferJournal* transfers, IInterruptedOperations* operations)
    : QObject(parent), history_(history), transfers_(transfers), operations_(operations) {}

// 선택한 작업의 파일 변경 범위를 확인 화면에 설명하는 함수.
QString RecoveryController::recoveryConfirmationText() const {
    if (!confirmation_) return {};
    const auto task = QString::fromStdString(confirmation_->planId);
    return confirmation_->action == RecoveryAction::Resume
        ? QStringLiteral("작업 %1을 재개할까요?\n현재 파일·설정·규칙을 다시 검사하고 남은 이동을 처리합니다. 다른 드라이브 전송은 검증 후 전송 원본을 제거할 수 있습니다.").arg(task)
        : QStringLiteral("작업 %1을 되돌릴까요?\n이미 이동한 파일만 원래 위치로 복원합니다. 미시작 원본은 그대로 두고, 온전한 원본이 남은 부분 임시 파일은 보존합니다.").arg(task);
}

// 실행 없이 표시된 승인 해시에 사용자 선택을 묶어 확인 대기를 만드는 함수.
void RecoveryController::requestRecoveryChoice(const QString& planId, const QString& action) {
    if (runner_ && runner_->busy()) return;
    if (busy_ || !operations_) return;
    const auto entry = std::find_if(recoveryCases_.begin(), recoveryCases_.end(), [&](const QVariant& value) {
        return value.toMap().value("whole").toBool() && value.toMap().value("planId").toString() == planId;
    });
    if (entry == recoveryCases_.end() || (action != "resume" && action != "rollback" && action != "later")) {
        recoveryMessage_ = QStringLiteral("작업 목록을 다시 검사한 뒤 선택해 주세요."); emit recoveryCasesChanged(); return;
    }
    const auto row = entry->toMap();
    if ((action == "resume" && !row.value("canResume").toBool()) || (action == "rollback" && !row.value("canRollback").toBool())) {
        recoveryMessage_ = QStringLiteral("현재 검사에서는 이 선택이 차단됐어요. 차단 이유를 확인해 주세요."); emit recoveryCasesChanged(); return;
    }
    RecoveryRequest request{planId.toStdString(), row.value("planHash").toString().toStdString(),
        action == "resume" ? RecoveryAction::Resume : action == "rollback" ? RecoveryAction::Rollback : RecoveryAction::Later};
    cancelRecoveryChoice();
    if (request.action == RecoveryAction::Later) { applyChoice(request); return; }
    confirmation_ = request; emit recoveryConfirmationChanged();
}

// 확인 창에서 승인한 스냅샷만 실행 요청으로 전달하는 함수.
void RecoveryController::confirmRecoveryChoice() {
    if (runner_ && runner_->busy()) return;
    if (busy_ || !confirmation_ || !operations_) return;
    const auto request = *confirmation_;
    cancelRecoveryChoice(); applyChoice(request);
}

// 확인 대기만 해제하고 백엔드 실행은 호출하지 않는 함수.
void RecoveryController::cancelRecoveryChoice() {
    if (!confirmation_) return;
    confirmation_.reset(); emit recoveryConfirmationChanged();
}

// 명시적 선택을 실행하고 최신 기록·화면 목록을 갱신하는 함수.
void RecoveryController::applyChoice(const RecoveryRequest& request) {
    busy_ = true; emit recoveryBusyChanged();
    dispatchOperation(request.action == RecoveryAction::Later ? nullptr : runner_, this,
        [operations = operations_, request] { return operations->execute(request); },
        [this, request](const Result<void>& result) {
        busy_ = false; emit recoveryBusyChanged();
        if (request.action == RecoveryAction::Later) {
            recoveryMessage_ = QStringLiteral("나중에 확인하도록 남겨뒀어요. 파일과 기록은 변경하지 않았어요."); emit recoveryCasesChanged(); return;
        }
        refreshRecoveryCases();
        recoveryMessage_ = result ? (request.action == RecoveryAction::Resume
            ? QStringLiteral("중단된 작업을 재개했어요. 기록 화면에서 결과를 확인할 수 있어요.")
            : QStringLiteral("이동된 파일을 되돌렸어요. 보존된 임시 파일이 있다면 확인 목록에 남습니다."))
            : QStringLiteral("복구를 완료하지 못했어요: %1").arg(recoveryError(result.error()));
        const auto id = QString::fromStdString(request.planId);
        emit recoveryAttempted(id);
        if (result) emit recoveryCompleted(id, request.action == RecoveryAction::Resume ? "resume" : "rollback");
        emit recoveryCasesChanged();
    });
}

// 진단·선택 후보·차단 이유를 작업당 한 항목으로 표시하는 함수.
void RecoveryController::refreshWholeCases() {
    const auto inspected = operations_->inspectAll();
    recoveryCases_.clear();
    if (!inspected) {
        cancelRecoveryChoice();
        recoveryMessage_ = QStringLiteral("중단 작업을 검사하지 못했어요: %1").arg(recoveryError(inspected.error()));
        setRecoveryNotice(recoveryMessage_); emit recoveryCasesChanged(); return;
    }
    // 구형 v1의 파일 변경 없는 기록 확정은 호환 경로로만 유지한다.
    RecoveryService legacy(history_, scanner_, hasher_, transfers_);
    const auto oldCases = legacy.inspectCases();
    for (const auto& item : inspected.value()) {
        const auto& report = item.inspection;
        QVariantMap row;
        row["whole"] = true; row["planId"] = QString::fromStdString(report.planId); row["planHash"] = QString::fromStdString(item.planHash);
        row["partIndex"] = 1; row["partCount"] = static_cast<int>(report.steps.size());
        row["pending"] = static_cast<int>(report.pending); row["moved"] = static_cast<int>(report.moved); row["restored"] = static_cast<int>(report.restored);
        row["needsAttention"] = static_cast<int>(report.needsAttention); row["preserved"] = static_cast<int>(report.preserved);
        row["canResume"] = item.canResume; row["canRollback"] = item.canRollback;
        row["canResolve"] = false;
        if (item.formatVersion == 1 && oldCases) for (const auto& old : oldCases.value())
            if (old.planId == report.planId && old.canResolve) row["canResolve"] = true;
        QStringList details;
        for (const auto& step : report.steps) for (const auto& reason : step.reasons)
            if (reason.code == "recovery.metadata_unsupported" && !details.contains(QStringLiteral("빠른 같은 볼륨 이동의 중단 복구는 아직 지원하지 않아요. 파일과 기록을 보존하며 별도 확인이 필요해요.")))
                details << QStringLiteral("빠른 같은 볼륨 이동의 중단 복구는 아직 지원하지 않아요. 파일과 기록을 보존하며 별도 확인이 필요해요.");
        if (report.status == PlanStatus::RolledBack && report.preserved)
            details << QStringLiteral("이동 복원은 끝났어요. 남은 부분 임시 파일의 별도 정리가 필요하며 이 작업을 다시 재개하지 않습니다.");
        else {
            if (item.resumeBlock) details << QStringLiteral("재개: %1").arg(recoveryError(*item.resumeBlock));
            if (item.rollbackBlock) details << QStringLiteral("되돌리기: %1").arg(recoveryError(*item.rollbackBlock));
        }
        if (report.preserved) details << QStringLiteral("부분 임시 파일 %1개를 보존 중이에요. 자동으로 삭제하지 않습니다.").arg(report.preserved);
        row["detail"] = details.isEmpty() ? QStringLiteral("표시 시점의 후보예요. 선택을 확인하면 실제 실행 전에 다시 검사합니다.") : details.join('\n');
        QVariantList files;
        for (const auto& step : report.steps) {
            QVariantMap file; file["source"] = displayPath(step.source); file["destination"] = displayPath(step.destination);
            file["state"] = recoveryStepText(step.state);
            if (step.transfer && (step.state == InterruptedStepState::Preserved || step.state == InterruptedStepState::NeedsAttention))
                file["staging"] = displayPath(step.transfer->attempt.staging);
            else file["staging"] = QString{};
            files.append(file);
        }
        row["files"] = files;
        recoveryCases_.append(row);
    }
    recoveryMessage_ = QStringLiteral("파일과 작업 기록을 변경하지 않고 중단 작업을 검사했어요.");
    setRecoveryNotice(recoveryCases_.isEmpty() ? QString{} : QStringLiteral("확인이 필요한 작업 %1건이 있어요. 재개·되돌리기는 직접 선택해야 합니다.").arg(recoveryCases_.size()));
    emit recoveryCasesChanged();
}

// 시작 시 발견한 미완료 작업 안내를 반환하는 함수.
QString RecoveryController::recoveryNotice() const { return recoveryNotice_; }

// 사용자가 확인할 중단 작업 목록을 반환하는 함수.
QVariantList RecoveryController::recoveryCases() const { return recoveryCases_; }

// 복구 화면의 마지막 검사 결과를 반환하는 함수.
QString RecoveryController::recoveryMessage() const { return recoveryMessage_; }

// 시작 시 복구 검사 결과를 화면에 전달하는 함수.
void RecoveryController::setRecoveryNotice(QString notice) {
    if (recoveryNotice_ == notice) return;
    recoveryNotice_ = std::move(notice);
    emit recoveryNoticeChanged();
}

// 중단 작업의 현재 원본·목적지 상태를 다시 읽는 함수.
void RecoveryController::refreshRecoveryCases() {
    if (runner_ && runner_->busy()) return;
    if (busy_) return;
    if (operations_) { refreshWholeCases(); return; }
    RecoveryService recovery(history_, scanner_, hasher_, transfers_);
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
        QString detail;
        if (item.requiresWholeRecovery) {
            detail = QStringLiteral("새 전체 작업 복구가 필요한 기록이에요. 기존 단계 상태를 유지하고 구형 기록 정리는 차단해요. 파일은 건드리지 않아요.");
        } else if (item.transferTracked) {
            detail = QStringLiteral("별도 전송 기록이 있어 원본·임시 파일·최종 파일의 확인이 필요해요. 기존 기록 정리는 차단하며 파일은 건드리지 않아요.");
        } else if (!item.contentHashAvailable) {
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
void RecoveryController::resolveRecoveryCase(const QString& planId) {
    if (runner_ && runner_->busy()) return;
    RecoveryService recovery(history_, scanner_, hasher_, transfers_);
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

// 실행 실패 뒤 중단 기록을 재조정하고 복구 목록을 갱신하는 함수.
void RecoveryController::reconcileAndRefreshCases() {
    if (operations_) { refreshRecoveryCases(); return; }
    RecoveryService recovery(history_, scanner_, hasher_, transfers_);
    (void)recovery.reconcile();
    refreshRecoveryCases();
}

} // namespace nookbit
