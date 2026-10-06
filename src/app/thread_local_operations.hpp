#pragma once
#include "infrastructure/filesystem/file_operation_service.hpp"
#include "infrastructure/filesystem/interrupted_operation_recovery_service.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/windows_transfer_recovery.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/runtime_settings_repository.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/plan_signature.hpp"
#include <nookbit/application/sort_workflow.hpp>
#include "infrastructure/filesystem/operation_mutex.hpp"
#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>

namespace nookbit {
/// 요청을 실행하는 스레드 안에서 모든 파일/DB 포트를 생성·해제하는 조립부.
class ThreadLocalOperations final : public IFileOperations, public IInterruptedOperations, public ISortWorkflow {
    QString database_, migration_, rules_, schema_, legacy_;
    std::filesystem::path appData_;
    ISettingsRepository& settings_;
    /// 화면의 Qt SQL 객체를 빌리지 않고 한 요청의 의존성을 구성하는 묶음.
    struct Session {
        SqliteHistoryRepository history;
        SqliteTransferJournal journal;
        JsonRuleSetRepository rules;
        FilesystemScanner scanner;
        QtPlanHasher hasher;
        RuleEngine engine;
        WindowsStagedCopy copy;
        WindowsTransferSourceRemoval removal;
        WindowsTransferFileInspector files;
        WindowsTransferRecoveryGuard guard;
        WindowsTransferCommitRecoveryGuard commitGuard;
        Planner planner;
        VerifySavedPlanUseCase verify;
        ExecuteTransferStepUseCase forward;
        RollbackTransferUseCase reverse;
        FileOperationService operations;
        SafeExecutor runner;
        InspectInterruptedPlanUseCase inspector;
        ResumeInterruptedTransferUseCase resolver;
        RecoverInterruptedPlanUseCase recover;
        InterruptedOperationRecoveryService interrupted;
        /// 현재 스레드 전용 저장소·실행기·복구기를 연결하는 함수.
        explicit Session(ThreadLocalOperations& owner)
            : history(owner.database_, owner.migration_), journal(owner.database_, owner.migration_),
              rules(owner.rules_, owner.schema_, owner.legacy_), planner(engine, owner.appData_, CrossVolumeDrafts::Allow),
              verify(rules, scanner, planner, hasher, history, &copy),
              forward(copy, removal, journal, history, hasher), reverse(journal, history, copy, removal, files, hasher),
              operations(verify, scanner, hasher, history, owner.appData_, &copy, &forward, &reverse),
              runner(owner.settings_, verify, scanner, hasher, history, owner.appData_, &copy, &forward, &reverse),
              inspector(history, journal, files, hasher, &copy), resolver(history, journal, copy, removal, files, guard, commitGuard, hasher),
              recover(inspector, history, owner.settings_, rules, hasher, runner, &resolver), interrupted(inspector, history, recover) {}
    };
public:
    /// 작업 스레드 전용 연결로 고정·검사를 끝내고 확인할 계획을 반환하는 함수.
    Result<OperationPlan> prepare(const OperationPlan& plan, bool frozen,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) override {
        OperationMutex lock;
        const auto acquired = lock.acquire();
        if (!acquired) return Result<OperationPlan>::failure(acquired.error());
        Session session(*this);
        auto saved = Result<OperationPlan>::success(plan);
        if (frozen) for (const auto& step : plan.steps) if (step.status != StepStatus::Skipped && step.movement && step.movement->strategy == MoveStrategy::VerifiedCopy) {
            const auto supported = session.copy.preflightSource(step.source);
            if (!supported) return Result<OperationPlan>::failure(supported.error());
            if (supported.value() && step.movement->preservation != PreservationPolicy::LocalNtfsBasicWithZone)
                return Result<OperationPlan>::failure({"transfer.zone_contract_required", "Download marker appeared after preparation. Create a new preview."});
        }
        if (!frozen) {
            auto draft = plan;
            for (auto& step : draft.steps) if (step.status != StepStatus::Skipped && step.movement && step.movement->strategy == MoveStrategy::VerifiedCopy) {
                const auto supported = session.copy.preflightSource(step.source);
                if (!supported) return Result<OperationPlan>::failure(supported.error());
                if (supported.value()) step.movement->preservation = PreservationPolicy::LocalNtfsBasicWithZone;
            }
            const bool sameVolumeOnly = std::all_of(draft.steps.begin(), draft.steps.end(), [](const MoveStep& step) {
                return step.movement && step.movement->strategy == MoveStrategy::Rename;
            });
            if (sameVolumeOnly) for (auto& step : draft.steps) step.movement->strategy = MoveStrategy::MetadataRename;
            FreezePreviewUseCase freeze(session.rules, session.scanner, session.planner, session.hasher, session.history, &session.copy);
            saved = freeze.execute(draft, selected, settings);
            if (!saved) return saved;
            // 갓 고정한 계획은 DB 서명/내용만 읽어 확인하고 같은 파일을 곧바로 다시 해싱하지 않는다.
            const auto readback = session.history.loadPlan(saved.value().id);
            if (!readback || readback.value().planHash != saved.value().planHash ||
                readback.value().status != PlanStatus::Planned ||
                readback.value().planHash != session.hasher.sha256(PlanSignature::approvalPayload(readback.value())) ||
                PlanSignature::planContents(readback.value()) != PlanSignature::planContents(saved.value()))
                return Result<OperationPlan>::failure({"sort.saved_plan_changed", "Saved confirmation plan differs from the frozen snapshot."});
            return readback;
        }
        if (!saved) return saved;
        return session.verify.execute(saved.value().id, selected, settings);
    }
    /// 사용자 확인 후 승인 검사와 파일 실행 모두 현재 작업 스레드에서 수행하는 함수.
    Result<void> approveAndExecute(const OperationPlan& plan,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) override {
        OperationMutex lock;
        const auto acquired = lock.acquire();
        if (!acquired) return acquired;
        Session session(*this);
        for (const auto& step : plan.steps) if (step.status != StepStatus::Skipped && step.movement && step.movement->strategy == MoveStrategy::VerifiedCopy) {
            const auto supported = session.copy.preflightSource(step.source);
            if (!supported) return Result<void>::failure(supported.error());
            if (supported.value() && step.movement->preservation != PreservationPolicy::LocalNtfsBasicWithZone)
                return Result<void>::failure({"transfer.zone_contract_required", "Download marker appeared after confirmation. Create a new preview."});
        }
        ApprovePlanUseCase approve(session.verify, session.history);
        const auto displayed = session.history.loadPlan(plan.id);
        if (!displayed || displayed.value().planHash != plan.planHash)
            return Result<void>::failure({"sort.confirmation_stale", "The displayed plan changed before approval."});
        const auto approval = approve.execute(plan.id, selected, settings);
        if (!approval) return Result<void>::failure(approval.error());
        const auto approved = session.history.loadPlan(plan.id);
        if (!approved) return Result<void>::failure(approved.error());
        if (approval.value().planHash != plan.planHash)
            return Result<void>::failure({"sort.confirmation_stale", "The approval differs from the displayed plan."});
        return session.operations.executeApproved(approved.value(), approval.value(), selected, settings);
    }
    /// 불변 경로와 스레드 안전한 현재 설정 포트만 보관하는 함수.
    ThreadLocalOperations(QString database, QString migration, QString rules, QString schema, QString legacy,
        std::filesystem::path appData, ISettingsRepository& settings)
        : database_(std::move(database)), migration_(std::move(migration)), rules_(std::move(rules)), schema_(std::move(schema)),
          legacy_(std::move(legacy)), appData_(std::move(appData)), settings_(settings) {}
    /// 승인된 값 스냅샷을 현재 스레드 전용 실행기로 전달하는 함수.
    Result<void> executeApproved(const OperationPlan& plan, const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selected, const UserSettings& settings) override {
        Session session(*this); return session.operations.executeApproved(plan, approval, selected, settings);
    }
    /// 저장된 계약으로 현재 스레드에서 안전하게 복원하는 함수.
    Result<void> rollback(const PlanId& id) override { Session session(*this); return session.operations.rollback(id); }
    /// 현재 스레드 전용 연결로 중단 작업을 읽기 전용 조회하는 함수.
    Result<std::vector<InterruptedOperationView>> inspectAll() override { Session session(*this); return session.interrupted.inspectAll(); }
    /// 명시적 복구 선택을 현재 스레드의 복구 서비스로 전달하는 함수.
    Result<void> execute(const RecoveryRequest& request) override {
        if (request.action == RecoveryAction::Later) return Result<void>::success();
        Session session(*this); return session.interrupted.execute(request);
    }
};
}
