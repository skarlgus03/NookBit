#pragma once

#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include <nookbit/application/ports.hpp>
#include <nookbit/application/transfer_journal.hpp>

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

public:
    /// 복구 기록 저장소를 받아 화면의 복구 작업을 준비하는 생성자.
    explicit RecoveryController(IHistoryRepository& history, QObject* parent = nullptr,
        const ITransferJournal* transfers = nullptr);

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

private:
    IHistoryRepository& history_;
    const ITransferJournal* transfers_;
    FilesystemScanner scanner_;
    QtPlanHasher hasher_;
    QString recoveryNotice_;
    QVariantList recoveryCases_;
    QString recoveryMessage_;
};

} // namespace nookbit
