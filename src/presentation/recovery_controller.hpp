#pragma once

#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>
#include <nookbit/application/interrupted_operations.hpp>

#include <QObject>
#include <QString>
#include <QVariantList>

namespace nookbit {

/// 중단 작업의 확인·기록 정리 상태를 화면에 제공하는 컨트롤러.
class RecoveryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recoveryNotice READ recoveryNotice NOTIFY recoveryNoticeChanged)
    Q_PROPERTY(QVariantList recoveryCases READ recoveryCases NOTIFY recoveryCasesChanged)
    Q_PROPERTY(QString recoveryMessage READ recoveryMessage NOTIFY recoveryCasesChanged)
    Q_PROPERTY(bool awaitingRecoveryConfirmation READ awaitingRecoveryConfirmation NOTIFY recoveryConfirmationChanged)
    Q_PROPERTY(QString recoveryConfirmationText READ recoveryConfirmationText NOTIFY recoveryConfirmationChanged)
    Q_PROPERTY(bool recoveryBusy READ recoveryBusy NOTIFY recoveryBusyChanged)

public:
    /// 복구 기록 저장소를 받아 화면의 복구 작업을 준비하는 생성자.
    explicit RecoveryController(IHistoryRepository& history, QObject* parent = nullptr,
        const ITransferJournal* transfers = nullptr, IInterruptedOperations* operations = nullptr);
    /// 실행 확인 대기 여부를 반환하는 함수.
    [[nodiscard]] bool awaitingRecoveryConfirmation() const { return confirmation_.has_value(); }
    /// 표시 당시의 선택·파일 변경 범위를 설명하는 함수.
    [[nodiscard]] QString recoveryConfirmationText() const;
    /// 중복 요청을 막기 위한 실행 중 여부를 반환하는 함수.
    [[nodiscard]] bool recoveryBusy() const { return busy_; }
    /// 표시된 승인 스냅샷으로 사용자 선택의 확인 창만 준비하는 함수.
    Q_INVOKABLE void requestRecoveryChoice(const QString& planId, const QString& action);
    /// 재개·되돌리기 확인을 명시적으로 수락한 뒤 백엔드 재검사를 요청하는 함수.
    Q_INVOKABLE void confirmRecoveryChoice();
    /// 확인 대기만 취소하며 파일·기록을 변경하지 않는 함수.
    Q_INVOKABLE void cancelRecoveryChoice();

    /// 시작 시 발견한 미완료 작업 안내를 반환하는 함수.
    [[nodiscard]] QString recoveryNotice() const;

    /// 사용자가 확인할 중단 작업 목록을 반환하는 함수.
    [[nodiscard]] QVariantList recoveryCases() const;

    /// 복구 화면의 마지막 검사 결과를 반환하는 함수.
    [[nodiscard]] QString recoveryMessage() const;

    /// 시작 시 복구 검사 결과를 화면에 전달하는 함수.
    void setRecoveryNotice(QString notice);

    /// 중단 작업의 현재 원본·목적지 상태를 다시 읽는 함수.
    Q_INVOKABLE void refreshRecoveryCases();

    /// 파일 위치를 다시 검증하고 확실한 작업 기록만 정리하는 함수.
    Q_INVOKABLE void resolveRecoveryCase(const QString& planId);

    /// 실행 실패 뒤 중단 기록을 재조정하고 복구 목록을 갱신하는 함수.
    void reconcileAndRefreshCases();

signals:
    void recoveryNoticeChanged();
    void recoveryCasesChanged();
    void recoveryConfirmationChanged();
    void recoveryBusyChanged();
    void recoveryAttempted(const QString& planId);
    void recoveryCompleted(const QString& planId, const QString& action);

private:
    IHistoryRepository& history_;
    const ITransferJournal* transfers_;
    IInterruptedOperations* operations_;
    std::optional<RecoveryRequest> confirmation_;
    bool busy_{false};
    /// 전체 작업 진단을 화면 값으로 변환하고 차단 이유를 표시하는 함수.
    void refreshWholeCases();
    /// 외부 복구 계약으로 명시적 선택만 실행하고 결과·목록을 갱신하는 함수.
    void applyChoice(const RecoveryRequest& request);
    FilesystemScanner scanner_;
    QtPlanHasher hasher_;
    QString recoveryNotice_;
    QVariantList recoveryCases_;
    QString recoveryMessage_;
};

} // namespace nookbit
