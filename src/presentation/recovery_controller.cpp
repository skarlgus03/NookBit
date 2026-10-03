#include "presentation/recovery_controller.hpp"

#include "infrastructure/filesystem/recovery_service.hpp"

#include <QSet>
#include <QVariantMap>

#include <utility>

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

} // namespace

// 복구 기록 저장소를 받아 화면의 복구 작업을 준비하는 생성자.
RecoveryController::RecoveryController(IHistoryRepository& history, QObject* parent, const ITransferJournal* transfers)
    : QObject(parent), history_(history), transfers_(transfers) {}

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
        if (item.transferTracked) {
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
    RecoveryService recovery(history_, scanner_, hasher_, transfers_);
    (void)recovery.reconcile();
    refreshRecoveryCases();
}

} // namespace nookbit
