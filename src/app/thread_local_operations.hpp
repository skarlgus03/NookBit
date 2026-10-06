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
#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>

namespace nookbit {
/// 요청을 실행하는 스레드 안에서 모든 파일/DB 포트를 생성·해제하는 조립부.
class ThreadLocalOperations final : public IFileOperations, public IInterruptedOperations {
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
