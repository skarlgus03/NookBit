#pragma once

#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "presentation/pending_files_model.hpp"
#include <nookbit/application/use_cases/create_preview.hpp>
#include <nookbit/application/use_cases/freeze_preview.hpp>
#include <nookbit/application/use_cases/verify_saved_plan.hpp>
#include <nookbit/application/use_cases/approve_plan.hpp>

#include <QObject>
#include <QString>
#include <QVariantList>

#include <filesystem>
#include <optional>

namespace nookbit {

/// 대기 파일의 미리보기와 계획 승인·실행·되돌리기를 연결하는 화면 컨트롤러.
class PreviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList steps READ steps NOTIFY previewChanged)
    Q_PROPERTY(QVariantList outcomes READ outcomes NOTIFY previewChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY previewChanged)
    Q_PROPERTY(QString message READ message NOTIFY previewChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
    Q_PROPERTY(bool hasFrozenPlan READ hasFrozenPlan NOTIFY previewChanged)
    Q_PROPERTY(bool hasApprovedPlan READ hasApprovedPlan NOTIFY previewChanged)
    Q_PROPERTY(bool hasMovableSteps READ hasMovableSteps NOTIFY previewChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY previewChanged)
    Q_PROPERTY(QString defaultDestinationRoot READ defaultDestinationRoot CONSTANT)
    Q_PROPERTY(QString activeRuleLabel READ activeRuleLabel NOTIFY activeRuleChanged)

public:
    /// 화면 모델과 계획 작업 계약 및 보호할 앱 데이터 경로를 받는 생성자.
    PreviewController(PendingFilesModel& pendingFiles,
                      ICreatePreviewUseCase& createPreview,
                      IFreezePreviewUseCase& freezePreview,
                      IVerifySavedPlanUseCase& verifySavedPlan,
                      IApprovePlanUseCase& approvePlan,
                      IHistoryRepository& history,
                      QString rulesPath,
                      QString schemaPath,
                      std::filesystem::path appDataRoot,
                      QString defaultDestinationRoot,
                      QObject* parent = nullptr,
                      QString legacySchemaPath = {});

    /// 화면에 표시할 파일별 이동 초안을 반환하는 함수.
    [[nodiscard]] QVariantList steps() const;

    /// 이동하지 않는 파일까지 포함한 파일별 미리보기 판단을 반환하는 함수.
    [[nodiscard]] QVariantList outcomes() const;

    /// 화면에 표시할 검사 경고를 반환하는 함수.
    [[nodiscard]] QStringList warnings() const;

    /// 최근 미리보기 결과 문구를 반환하는 함수.
    [[nodiscard]] QString message() const;

    /// 유효한 미리보기 초안의 존재 여부를 반환하는 함수.
    [[nodiscard]] bool hasPreview() const noexcept;

    /// 현재 미리보기를 SQLite에 고정 저장했는지 반환하는 함수.
    [[nodiscard]] bool hasFrozenPlan() const noexcept;

    [[nodiscard]] bool hasApprovedPlan() const noexcept;

    /// 현재 계획에 실제 이동할 파일이 있는지 반환하는 함수.
    [[nodiscard]] bool hasMovableSteps() const noexcept;

    [[nodiscard]] bool canUndo() const noexcept;

    /// 처음 제안할 정리 목적지 루트를 반환하는 함수.
    [[nodiscard]] QString defaultDestinationRoot() const;

    /// 현재 저장되어 실제 미리보기에 적용되는 규칙의 이름과 버전을 반환하는 함수.
    [[nodiscard]] QString activeRuleLabel() const;

    /// 저장된 규칙을 다시 읽어 적용 중인 규칙 표시와 낡은 미리보기를 갱신하는 함수.
    void refreshActiveRules();

    /// 대기 파일에 대해 실제 이동 없이 분류 미리보기를 만드는 함수.
    Q_INVOKABLE void createPreview(const QString& destinationRoot);

    /// 현재 미리보기를 재검사한 뒤 승인 전 계획으로 고정·저장하는 함수.
    Q_INVOKABLE void freezePreview();

    /// 저장된 계획을 DB에서 다시 읽고 현재 상태와 비교하는 함수.
    Q_INVOKABLE bool verifySavedPlan();

    /// 저장된 파일 계획을 다시 검사하고 명시적 승인을 기록하는 함수.
    Q_INVOKABLE void approvePlan();

    /// 승인된 파일들을 다시 검사한 뒤 순서대로 이동하는 함수.
    Q_INVOKABLE void moveApprovedFile();

    /// 마지막으로 이동한 계획의 파일들을 원래 위치로 되돌리는 함수.
    Q_INVOKABLE void undoLastMove();

    /// 기록에서 되돌린 작업이 마지막 이동이면 버튼 상태를 갱신하는 함수.
    void refreshLastUndoAfterHistoryRollback(const QString& planId);

    /// 대기 목록이 바뀌었을 때 낡은 미리보기를 지우는 함수.
    Q_INVOKABLE void clear();

signals:
    void previewChanged();
    void activeRuleChanged();
    void destinationRootAccepted(const QString& root);
    void recoveryReconcileRequested();

private:
    /// 실패 이유를 화면에 알리고 기존 초안을 지우는 함수.
    void showError(const QString& message);

    PendingFilesModel& pendingFiles_;
    ICreatePreviewUseCase& createPreview_;
    IFreezePreviewUseCase& freezePreview_;
    IVerifySavedPlanUseCase& verifySavedPlan_;
    IApprovePlanUseCase& approvePlan_;
    JsonRuleSetRepository rules_;
    FilesystemScanner scanner_;
    RuleEngine ruleEngine_;
    Planner planner_;
    QtPlanHasher hasher_;
    IHistoryRepository& history_;
    std::filesystem::path appDataRoot_;
    QString defaultDestinationRoot_;
    QString activeRuleLabel_;
    QString previewHomeRoot_;
    std::optional<OperationPlan> draft_;
    std::optional<ApprovalRecord> approval_;
    std::optional<PlanId> undoPlanId_;
    QVariantList steps_;
    QVariantList outcomes_;
    QStringList warnings_;
    QString message_;
    bool hasPreview_{false};
    bool hasFrozenPlan_{false};
};

} // namespace nookbit
