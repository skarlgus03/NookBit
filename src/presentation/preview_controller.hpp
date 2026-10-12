#pragma once

#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include <nookbit/application/file_operations.hpp>
#include "presentation/pending_files_model.hpp"
#include <nookbit/application/use_cases/create_preview.hpp>
#include <nookbit/application/use_cases/freeze_preview.hpp>
#include <nookbit/application/use_cases/verify_saved_plan.hpp>
#include <nookbit/application/use_cases/approve_plan.hpp>

#include <QObject>
#include "presentation/background_task_runner.hpp"
#include <nookbit/application/sort_workflow.hpp>
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
    Q_PROPERTY(bool awaitingConfirmation READ awaitingConfirmation NOTIFY previewChanged)
    Q_PROPERTY(QVariantList confirmationSteps READ confirmationSteps NOTIFY previewChanged)
    Q_PROPERTY(QString defaultDestinationRoot READ defaultDestinationRoot CONSTANT)
    Q_PROPERTY(QString activeRuleLabel READ activeRuleLabel NOTIFY activeRuleChanged)
    Q_PROPERTY(QString collisionPolicy READ collisionPolicy WRITE setCollisionPolicy NOTIFY collisionPolicyChanged)

public:
    /// 앱 조립부의 단일 백그라운드 실행기를 빌려 받는 함수.
    void setTaskRunner(BackgroundTaskRunner* runner) { runner_ = runner; }
    /// 확인 전후의 무거운 검사를 화면 밖으로 전달하는 계약을 받는 함수.
    void setSortWorkflow(ISortWorkflow* workflow) { sortWorkflow_ = workflow; }
    /// 화면 모델·계획 작업·파일 실행 계약과 기본 목적지를 받는 생성자.
    PreviewController(PendingFilesModel& pendingFiles,
                      ICreatePreviewUseCase& createPreview,
                      IFreezePreviewUseCase& freezePreview,
                      IVerifySavedPlanUseCase& verifySavedPlan,
                      IApprovePlanUseCase& approvePlan,
                      IHistoryRepository& history,
                      IFileOperations& operations,
                      QString rulesPath,
                      QString schemaPath,
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

    /// 규칙이 기본값을 상속할 때 적용할 공통 충돌 정책을 반환하는 함수.
    [[nodiscard]] QString collisionPolicy() const;

    /// 공통 충돌 정책을 바꾸고 이전 정책으로 만든 계획·승인을 지우는 함수.
    void setCollisionPolicy(const QString& policy);

    /// 저장된 규칙을 다시 읽어 적용 중인 규칙 표시와 낡은 미리보기를 갱신하는 함수.
    void refreshActiveRules();
    /// 사용자 설정의 외부 목적지 허용 목록을 반영하고 이전 미리보기를 무효화하는 함수.
    void setAllowedDestinationRoots(std::vector<std::filesystem::path> roots) {
        allowedDestinationRoots_ = std::move(roots);
        clear();
    }

    /// 대기 파일에 대해 실제 이동 없이 분류 미리보기를 만드는 함수.
    Q_INVOKABLE void createPreview(const QString& destinationRoot);

    /// 계획을 고정·검사하고 실제 이동 없이 최종 확인을 준비하는 함수.
    Q_INVOKABLE bool prepareSort();

    /// 준비된 최종 확인을 취소하고 파일을 그대로 두는 함수.
    Q_INVOKABLE void cancelSort();

    /// 사용자가 확인한 고정 계획만 다시 검사·승인하고 실행하는 함수.
    Q_INVOKABLE void confirmSort();

    /// 사용자의 최종 확인을 기다리는지 반환하는 함수.
    [[nodiscard]] bool awaitingConfirmation() const noexcept;

    /// 고정된 계획에서 실제 이동할 원본·목적지만 반환하는 함수.
    [[nodiscard]] QVariantList confirmationSteps() const;

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
    /// 실제 정리 실행의 시작과 결과만 알리고 어떤 화면을 열지는 결정하지 않는다.
    void sortExecutionStarted();
    void sortExecutionFinished(bool success, const QString& message);
    void previewChanged();
    void activeRuleChanged();
    void collisionPolicyChanged();
    void destinationRootAccepted(const QString& root);
    void recoveryReconcileRequested();

private:
    ISortWorkflow* sortWorkflow_{nullptr};
    /// 선택 경로와 현재 설정을 값으로 고정해 비동기 확인 준비를 요청하는 함수.
    void prepareSortInBackground();
    /// 고정 계획의 사용자 확인 이후 승인·이동을 비동기로 요청하는 함수.
    void confirmSortInBackground();
    /// 이동 결과를 화면 스레드에서만 대기 목록·기록 안내에 반영하는 함수.
    void finishMovement(const PlanId& id, const std::vector<MoveStep>& steps, const Result<void>& result);
    BackgroundTaskRunner* runner_{nullptr};
    /// 실패 이유를 화면에 알리고 기존 초안을 지우는 함수.
    void showError(const QString& message);

    PendingFilesModel& pendingFiles_;
    ICreatePreviewUseCase& createPreview_;
    IFreezePreviewUseCase& freezePreview_;
    IVerifySavedPlanUseCase& verifySavedPlan_;
    IApprovePlanUseCase& approvePlan_;
    JsonRuleSetRepository rules_;
    IHistoryRepository& history_;
    IFileOperations& operations_;
    QString defaultDestinationRoot_;
    QString activeRuleLabel_;
    CollisionPolicy collisionPolicy_{CollisionPolicy::Rename};
    QString previewHomeRoot_;
    std::vector<std::filesystem::path> allowedDestinationRoots_;
    std::optional<OperationPlan> draft_;
    std::optional<ApprovalRecord> approval_;
    std::optional<PlanId> undoPlanId_;
    QVariantList steps_;
    QVariantList outcomes_;
    QStringList warnings_;
    QString message_;
    bool hasPreview_{false};
    bool hasFrozenPlan_{false};
    bool awaitingConfirmation_{false};
};

} // namespace nookbit
