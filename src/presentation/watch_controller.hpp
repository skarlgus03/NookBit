#pragma once

#include "presentation/pending_files_model.hpp"

#include <nookbit/application/ports.hpp>

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace nookbit {

/// 사용자 감시 조건을 저장하고 충족 시 대기 등록만 제안한다.
class WatchController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY settingsChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
    Q_PROPERTY(bool recursive READ recursive NOTIFY settingsChanged)
    Q_PROPERTY(int rescanIntervalMinutes READ rescanIntervalMinutes NOTIFY settingsChanged)
    Q_PROPERTY(bool combineAll READ combineAll NOTIFY settingsChanged)
    Q_PROPERTY(bool countEnabled READ countEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int minimumFileCount READ minimumFileCount NOTIFY settingsChanged)
    Q_PROPERTY(bool sizeEnabled READ sizeEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int minimumTotalMiB READ minimumTotalMiB NOTIFY settingsChanged)
    Q_PROPERTY(int minimumTotalSize READ minimumTotalSize NOTIFY settingsChanged)
    Q_PROPERTY(QString totalSizeUnit READ totalSizeUnit NOTIFY settingsChanged)
    Q_PROPERTY(bool ageEnabled READ ageEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int minimumAgeMinutes READ minimumAgeMinutes NOTIFY settingsChanged)
    Q_PROPERTY(QString excludePatterns READ excludePatterns NOTIFY settingsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY watchChanged)
    Q_PROPERTY(bool proposalVisible READ proposalVisible NOTIFY watchChanged)
    Q_PROPERTY(QString proposalText READ proposalText NOTIFY watchChanged)
    Q_PROPERTY(QStringList proposalFiles READ proposalFiles NOTIFY watchChanged)

public:
    /// 대기 목록과 감시 설정·제안 계약을 연결하는 생성자.
    WatchController(PendingFilesModel& pendingFiles,
                    IWatchSettingsRepository& repository,
                    IWatchProposalUseCase& evaluateWatchFolder,
                    QString defaultFolder,
                    QObject* parent = nullptr);

    /// 현재 설정된 감시 폴더를 반환하는 함수.
    [[nodiscard]] QString folder() const;
    /// 자동 감시 사용 여부를 반환하는 함수.
    [[nodiscard]] bool enabled() const noexcept;
    /// 하위 폴더 감시 여부를 반환하는 함수.
    [[nodiscard]] bool recursive() const noexcept;
    /// 정기 재검사 간격을 반환하는 함수.
    [[nodiscard]] int rescanIntervalMinutes() const noexcept;
    /// 모든 조건을 충족해야 하는지 반환하는 함수.
    [[nodiscard]] bool combineAll() const noexcept;
    /// 파일 수 조건 사용 여부를 반환하는 함수.
    [[nodiscard]] bool countEnabled() const noexcept;
    /// 파일 수 조건의 기준값을 반환하는 함수.
    [[nodiscard]] int minimumFileCount() const noexcept;
    /// 전체 크기 조건 사용 여부를 반환하는 함수.
    [[nodiscard]] bool sizeEnabled() const noexcept;
    /// 전체 크기 조건의 MiB 기준값을 반환하는 함수.
    [[nodiscard]] int minimumTotalMiB() const noexcept;
    /// 단위 선택과 함께 표시할 전체 크기 숫자를 반환하는 함수.
    [[nodiscard]] int minimumTotalSize() const noexcept;
    /// 저장된 크기 단위를 반환하며 이전 MiB 설정은 그대로 유지하는 함수.
    [[nodiscard]] QString totalSizeUnit() const;
    /// 가장 오래된 파일의 경과 시간 조건 사용 여부를 반환하는 함수.
    [[nodiscard]] bool ageEnabled() const noexcept;
    /// 경과 시간 조건의 분 기준값을 반환하는 함수.
    [[nodiscard]] int minimumAgeMinutes() const noexcept;
    /// 제외할 파일명 패턴을 쉼표로 이어 반환하는 함수.
    [[nodiscard]] QString excludePatterns() const;
    /// 최근 감시·검사 결과 문구를 반환하는 함수.
    [[nodiscard]] QString statusMessage() const;
    /// 현재 제안이 열려 있는지 반환하는 함수.
    [[nodiscard]] bool proposalVisible() const noexcept;
    /// 현재 제안의 요약 문구를 반환하는 함수.
    [[nodiscard]] QString proposalText() const;
    /// 제안에 포함된 파일의 경로를 반환하는 함수.
    [[nodiscard]] QStringList proposalFiles() const;
    /// 정리 제안을 쉬는 종료 시각을 UTC 밀리초로 반환하는 함수.
    [[nodiscard]] qint64 snoozedUntilUtcMs() const noexcept;
    /// 저장된 제안 대기 시각을 복원하고 재검사 타이머를 준비하는 함수.
    void restoreSnoozeUntil(qint64 deadlineUtcMs);

    /// 화면에서 고른 폴더와 조건을 검증해 저장하는 함수.
    Q_INVOKABLE void saveSettings(const QString& folder,
                                  bool enabled,
                                  bool recursive,
                                  int intervalMinutes,
                                  bool combineAll,
                                  bool countEnabled,
                                  int minimumFileCount,
                                  bool sizeEnabled,
                                  int minimumTotalMiB,
                                  bool ageEnabled,
                                  int minimumAgeMinutes,
                                  const QString& excludePatterns,
                                  const QString& sizeUnit = QStringLiteral("MiB"));

    /// 저장된 조건으로 감시 폴더를 즉시 읽어 제안 여부를 판단하는 함수.
    Q_INVOKABLE void checkNow();
    /// 현재 제안의 파일들을 이동 없이 대기 목록에 등록하는 함수.
    Q_INVOKABLE void addProposalToPending();
    /// 현재 제안을 닫고 같은 파일 상태에서는 다시 묻지 않는 함수.
    Q_INVOKABLE void dismissProposal();
    /// 현재 제안을 닫고 30분 뒤 현재 조건으로 다시 확인하는 함수.
    Q_INVOKABLE void snoozeProposal();
    /// 분류 규칙 저장 후 이전 제안을 지우고 새 규칙으로 다시 평가하는 함수.
    void rulesChanged();

signals:
    void settingsChanged();
    void watchChanged();
    void snoozeDeadlineChanged(qint64 deadlineUtcMs);

private:
    /// 저장된 조건을 불러오고 기본값을 준비하는 함수.
    void loadSettings(const QString& defaultFolder);
    /// 설정에 맞게 폴더 변경 감지와 정기 검사를 다시 시작하는 함수.
    void restartMonitoring();
    /// 사용자에게 표시 중인 제안을 지우는 함수.
    void clearProposal();

    PendingFilesModel& pendingFiles_;
    IWatchSettingsRepository& repository_;
    IWatchProposalUseCase& evaluateWatchFolder_;
    QFileSystemWatcher directoryWatcher_;
    QTimer rescanTimer_;
    QTimer debounceTimer_;
    QTimer readinessTimer_;
    QTimer snoozeTimer_;
    qint64 snoozedUntilUtcMs_{0};
    WatchSettings settings_;
    QString statusMessage_;
    QString proposalText_;
    QStringList proposedFiles_;
    QString lastOfferedSignature_;
    bool proposalVisible_{false};
};

} // namespace nookbit
