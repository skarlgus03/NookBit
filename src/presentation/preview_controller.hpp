#pragma once

#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "presentation/pending_files_model.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <filesystem>
#include <optional>

namespace nookbit {

/// 대기 파일을 읽기 전용 미리보기 유스케이스에 연결하는 화면 컨트롤러.
class PreviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList steps READ steps NOTIFY previewChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY previewChanged)
    Q_PROPERTY(QString message READ message NOTIFY previewChanged)
    Q_PROPERTY(QString recoveryNotice READ recoveryNotice NOTIFY recoveryNoticeChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
    Q_PROPERTY(bool hasFrozenPlan READ hasFrozenPlan NOTIFY previewChanged)
    Q_PROPERTY(QString defaultDestinationRoot READ defaultDestinationRoot CONSTANT)

public:
    /// 화면 모델과 규칙 파일 및 보호할 앱 데이터 경로를 받는 생성자.
    PreviewController(PendingFilesModel& pendingFiles,
                      QString rulesPath,
                      QString schemaPath,
                      std::filesystem::path appDataRoot,
                      QString defaultDestinationRoot,
                      QString databasePath,
                      QString migrationPath,
                      QObject* parent = nullptr);

    /// 화면에 표시할 파일별 이동 초안을 반환하는 함수.
    [[nodiscard]] QVariantList steps() const;

    /// 화면에 표시할 검사 경고를 반환하는 함수.
    [[nodiscard]] QStringList warnings() const;

    /// 최근 미리보기 결과 문구를 반환하는 함수.
    [[nodiscard]] QString message() const;

    /// 시작 시 발견한 미완료 작업 안내를 반환하는 함수.
    [[nodiscard]] QString recoveryNotice() const;

    /// 시작 시 복구 검사 결과를 화면에 전달하는 함수.
    void setRecoveryNotice(QString notice);

    /// 유효한 미리보기 초안의 존재 여부를 반환하는 함수.
    [[nodiscard]] bool hasPreview() const noexcept;

    /// 현재 미리보기를 SQLite에 고정 저장했는지 반환하는 함수.
    [[nodiscard]] bool hasFrozenPlan() const noexcept;

    /// 처음 제안할 정리 목적지 루트를 반환하는 함수.
    [[nodiscard]] QString defaultDestinationRoot() const;

    /// 대기 파일에 대해 실제 이동 없이 분류 미리보기를 만드는 함수.
    Q_INVOKABLE void createPreview(const QString& destinationRoot);

    /// 현재 미리보기를 재검사한 뒤 승인 전 계획으로 고정·저장하는 함수.
    Q_INVOKABLE void freezePreview();

    /// 저장된 계획을 DB에서 다시 읽고 현재 상태와 비교하는 함수.
    Q_INVOKABLE bool verifySavedPlan();

    /// 대기 목록이 바뀌었을 때 낡은 미리보기를 지우는 함수.
    Q_INVOKABLE void clear();

signals:
    void previewChanged();
    void recoveryNoticeChanged();

private:
    /// 실패 이유를 화면에 알리고 기존 초안을 지우는 함수.
    void showError(const QString& message);

    PendingFilesModel& pendingFiles_;
    JsonRuleSetRepository rules_;
    FilesystemScanner scanner_;
    RuleEngine ruleEngine_;
    Planner planner_;
    QtPlanHasher hasher_;
    SqliteHistoryRepository history_;
    QString defaultDestinationRoot_;
    QString previewHomeRoot_;
    std::optional<OperationPlan> draft_;
    QVariantList steps_;
    QStringList warnings_;
    QString message_;
    QString recoveryNotice_;
    bool hasPreview_{false};
    bool hasFrozenPlan_{false};
};

} // namespace nookbit
