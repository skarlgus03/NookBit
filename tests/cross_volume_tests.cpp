#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include <nookbit/application/rollback_transfer_use_case.hpp>
#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>
#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>
#include "infrastructure/filesystem/interrupted_operation_recovery_service.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/filesystem/windows_file_mover.hpp"
#include "modules/planner/planner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/windows_transfer_recovery.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
using namespace nookbit;

// 실패한 계약의 이유를 예외로 전달해 해당 시나리오를 즉시 멈추는 함수.
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
// 실패한 포트의 실제 오류를 잃지 않고 결과를 꺼내는 함수.
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.error().code + ": " + result.error().message);
    return std::move(result.value());
}
// 기록 포트의 성공 여부를 확인하는 함수.
void saved(Result<void> result) {
    if (!result) throw std::runtime_error(result.error().code + ": " + result.error().message);
}

// 부모에게 정확한 중단 지점을 알리고 강제 종료까지 자식만 대기시키는 함수.
void checkpoint(const QString& name) {
    std::cout << "CHECKPOINT " << name.toStdString() << " " << QCoreApplication::applicationPid() << std::endl;
    QSemaphore stopped;
    stopped.acquire(); // 부모가 이 자식의 QProcess 핸들만 종료하며 정상 반환하지 않는다.
}
// 테스트가 소유한 새 경로만 생성하고 기존 파일 덮어쓰기를 금지하는 함수.
void createFile(const std::filesystem::path& path, const std::string& contents) {
    require(!std::filesystem::exists(path), "Test file already exists");
    std::ofstream file(path, std::ios::binary);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    require(!file.fail(), "Cannot create owned test file");
}
// 실제 Windows 객체 ID와 전체 SHA-256을 읽는 함수.
FileFingerprint fingerprint(const std::filesystem::path& path) {
    FilesystemScanner scanner;
    QtPlanHasher hasher;
    return take(scanFingerprint(scanner, path, &hasher));
}

// 파일 작업을 바꾸지 않고 명시한 결과 기록 한 번만 실패시키는 테스트 대역.
class FaultJournal final : public ITransferJournal {
    ITransferJournal& inner_;
public:
    std::optional<TransferPhase> failAt;
    QString stopAt;
    std::optional<TransferPhase> throwAt;
    bool throwBeforeCompletion{false};
    // 실제 SQLite 저널을 빌려 받는 함수.
    explicit FaultJournal(ITransferJournal& inner) : inner_(inner) {}
    // 새 시도 기록을 전달하는 함수.
    Result<void> begin(const TransferAttempt& attempt) override { return inner_.begin(attempt); }
    // 실제 파일 작업 뒤 지정한 기록 경계에서 한 번만 실패하는 함수.
    Result<void> advance(const std::string& id, const TransferUpdate& update) override {
        if (stopAt == "before-copy" && update.nextPhase == TransferPhase::CopyIntent) checkpoint(stopAt);
        if (throwAt == update.nextPhase) throw std::runtime_error("owned-transfer-stop");
        if ((stopAt == "after-rename" && update.nextPhase == TransferPhase::DestinationCommitted) ||
            (stopAt == "after-remove" && update.nextPhase == TransferPhase::SourceRemoved)) checkpoint(stopAt);
        if (failAt == update.nextPhase) {
            failAt.reset();
            return Result<void>::failure({"test.record_interrupted", "Injected result record interruption"});
        }
        return inner_.advance(id, update);
    }
    // 시도 증거를 복원하는 함수.
    Result<TransferAttempt> load(const std::string& id) const override { return inner_.load(id); }
    // 같은 단계의 전송 시도를 조회하는 함수.
    Result<std::vector<TransferAttempt>> forStep(const StepId& id) const override { return inner_.forStep(id); }
    // 원자적 정방향 완료를 전달하는 함수.
    Result<void> completeForward(const std::string& id) override {
        if (throwBeforeCompletion) throw std::runtime_error("owned-transfer-stop");
        if (stopAt == "before-complete") checkpoint(stopAt);
        if (failAt == TransferPhase::Completed) {
            failAt.reset();
            return Result<void>::failure({"test.completion_interrupted", "Injected atomic completion failure"});
        }
        return inner_.completeForward(id);
    }
    // 원자적 역방향 완료를 전달하는 함수.
    Result<void> completeRollback(const std::string& id) override { return inner_.completeRollback(id); }
};

// 실제 복사/검증은 유지하고 한 전송의 취소 또는 기록 경계만 중단하는 시험 실행 포트.
class InterruptedCopyStep final : public ITransferStepExecutor {
    ExecuteTransferStepUseCase& normal_;
    const IHistoryRepository& history_;
    IStagedCopy& copy_;
    ITransferSourceRemoval& removal_;
    FaultJournal& journal_;
    const IPlanHasher& hasher_;
public:
    StepId target;
    bool partial{false};
    bool interruptedOnce{false};
    QString crashAt;
    // 시험용 중단 지점만 조율하고 실제 구현체는 외부에서 빌려 받는 함수.
    InterruptedCopyStep(ExecuteTransferStepUseCase& normal, const IHistoryRepository& history, IStagedCopy& copy,
        ITransferSourceRemoval& removal, FaultJournal& journal, const IPlanHasher& hasher)
        : normal_(normal), history_(history), copy_(copy), removal_(removal), journal_(journal), hasher_(hasher) {}
    // 지정한 복사 단계에서만 실제 1MiB 기록 뒤 취소하거나 정확한 결과 기록 경계에서 중단하는 함수.
    Result<void> execute(const PlanId& planId, const StepId& stepId, const std::string& attemptId,
        const std::filesystem::path& staging) override {
        if (stepId != target || interruptedOnce) return normal_.execute(planId, stepId, attemptId, staging);
        interruptedOnce = true;
        if (!crashAt.isEmpty()) journal_.stopAt = crashAt;
        if (!partial) {
            ExecuteTransferStepUseCase stopped(copy_, removal_, journal_, history_, hasher_);
            return stopped.execute(planId, stepId, attemptId, staging);
        }
        const auto plan = take(history_.loadPlan(planId));
        const auto step = std::find_if(plan.steps.begin(), plan.steps.end(), [&](const MoveStep& item) { return item.id == stepId; });
        require(step != plan.steps.end(), "Owned partial step missing");
        TransferAttempt attempt;
        attempt.id = attemptId; attempt.planId = planId; attempt.stepId = stepId;
        attempt.source = step->source; attempt.destination = step->destination; attempt.staging = staging;
        attempt.sourceFingerprint = step->fingerprint;
        int checks = 0;
        CommitTransferCopyUseCase copy(copy_, journal_);
        // 첫 블록 기록 뒤 취소 요청을 반환하는 함수.
        const auto result = copy.execute(attempt, [&] {
            if (crashAt == "mid-copy" && std::filesystem::exists(staging) && std::filesystem::file_size(staging) >= 1024 * 1024)
                checkpoint(crashAt);
            return ++checks >= 3;
        }, &*step->movement);
        require(!result && std::filesystem::file_size(staging) == 1024 * 1024, "Expected owned 1MiB partial transfer");
        return Result<void>::failure(result.error());
    }
};

// 한 시나리오의 새 파일·새 승인 계획·새 DB만 소유하는 테스트 환경.
struct Fixture {
    std::filesystem::path sourceRoot, targetRoot;
    QString database;
    QString migration{QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql"};
    SqliteHistoryRepository history;
    SqliteTransferJournal journal;
    FaultJournal faults;
    WindowsStagedCopy copy;
    WindowsTransferSourceRemoval removal;
    WindowsTransferFileInspector files;
    QtPlanHasher hasher;
    OperationPlan plan;
    TransferAttempt attempt;
    // 전용 폴더 안의 이진 파일과 실제 서로 다른 볼륨의 v2 승인 계약을 만드는 함수.
    Fixture(const QString& sourceRun, const QString& targetRun, const QString& name, bool withZone = false)
        : sourceRoot((QDir(sourceRun).filePath(name)).toStdWString()),
          targetRoot((QDir(targetRun).filePath(name)).toStdWString()),
          database(QDir(targetRun).filePath(name + "/history.sqlite")),
          history(database, migration), journal(database, migration), faults(journal) {
        require(std::filesystem::create_directory(sourceRoot) && std::filesystem::create_directory(targetRoot), "Cannot create unique scenario folders");
        std::string contents(4 * 1024 * 1024 + 37, '\0');
        for (size_t i = 0; i < contents.size(); ++i) contents[i] = static_cast<char>((i * 31 + i / 257) % 256);
        attempt = {};
        attempt.id = "forward"; attempt.planId = "plan"; attempt.stepId = "step";
        attempt.source = sourceRoot / L"테스트 파일.bin";
        attempt.destination = targetRoot / "new-parent" / "nested" / L"정리 파일.bin";
        attempt.staging = attempt.destination.parent_path() / "stage.tmp";
        createFile(attempt.source, contents);
        if (withZone) createFile(std::filesystem::path(attempt.source.wstring() + L":Zone.Identifier"),
            "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.invalid/owned-recovery\r\n");
        attempt.sourceFingerprint = fingerprint(attempt.source);
        OperationPlan draft;
        draft.id = attempt.planId; draft.ruleSetVersion = "cross-volume-test";
        draft.settingsRevision = std::string(64, 'b'); draft.rulesRevision = std::string(64, 'c');
        MoveStep step;
        step.id = attempt.stepId; step.source = attempt.source; step.destination = attempt.destination;
        step.fingerprint = attempt.sourceFingerprint; step.matchedRuleId = "test-rule";
        draft.steps.push_back(step);
        PrepareTransferPlanUseCase prepare(copy);
        plan = take(prepare.execute(draft));
        require(plan.formatVersion == 2 && plan.steps[0].movement->strategy == MoveStrategy::VerifiedCopy &&
            plan.steps[0].movement->sourceVolume.guidPath != plan.steps[0].movement->destinationVolume.guidPath,
            "Real distinct-volume VerifiedCopy contract required; no forced strategy is allowed");
        plan.status = PlanStatus::Planned;
        if (withZone) plan.steps[0].movement->preservation = PreservationPolicy::LocalNtfsBasicWithZone;
        plan.planHash = hasher.sha256(PlanSignature::approvalPayload(plan));
        saved(history.savePlan(plan));
        saved(history.saveApproval({plan.id, plan.planHash, std::chrono::system_clock::now()}));
        saved(history.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing}));
        saved(history.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::IntentRecorded}));
        saved(prepare.verify(take(history.loadPlan(plan.id))));
    }
    // 승인된 실제 볼륨 계약으로 새 복사 시도를 수행하는 함수.
    Result<TransferAttempt> commit(const std::function<bool()>& cancelled = {}) {
        CommitTransferCopyUseCase useCase(copy, faults);
        return useCase.execute(attempt, cancelled, &*plan.steps[0].movement);
    }
    // 별도 원본 제거와 계획 완료를 요청하는 함수.
    Result<TransferAttempt> finish() {
        FinalizeTransferUseCase useCase(faults, removal, history, hasher);
        return useCase.execute(attempt.id);
    }
    // 전체 이동 결과와 서로 다른 원본/복사 객체를 확인하는 함수.
    void completed() {
        const auto state = take(journal.load(attempt.id));
        const auto savedPlan = take(history.loadPlan(plan.id));
        require(state.phase == TransferPhase::Completed && savedPlan.status == PlanStatus::Completed &&
            savedPlan.steps[0].status == StepStatus::Applied, "Atomic movement completion missing");
        require(!std::filesystem::exists(attempt.source) && !std::filesystem::exists(attempt.staging), "Original or stage remains after completion");
        const auto actual = fingerprint(attempt.destination);
        require(TransferValidation::sameContent(attempt.sourceFingerprint, actual) &&
            !TransferValidation::sameObject(attempt.sourceFingerprint, actual) &&
            state.destinationFingerprint && sameFingerprint(*state.destinationFingerprint, actual), "Final identity or binary contents differ");
    }
    // 정상 되돌리기로 원래 볼륨·경로·내용·기록을 확인하는 함수.
    void rollback() {
        RollbackTransferUseCase undo(journal, history, copy, removal, files, hasher);
        const auto result = take(undo.execute(attempt.id, "reverse", sourceRoot / "reverse-stage.tmp"));
        const auto savedPlan = take(history.loadPlan(plan.id));
        require(result.direction == TransferDirection::Rollback && result.phase == TransferPhase::Completed &&
            savedPlan.status == PlanStatus::RolledBack && savedPlan.steps[0].status == StepStatus::RolledBack, "Rollback records differ");
        require(!std::filesystem::exists(attempt.destination) && TransferValidation::sameContent(attempt.sourceFingerprint, fingerprint(attempt.source)),
            "Rollback location or full contents differ");
    }
};

// 실제 두 볼륨의 다운로드 표식 복구와 증거 부족·표식 불일치 보존을 검사하는 함수.
void zoneRecoveryScenario(const QString& sourceRun, const QString& targetRun, int index) {
    Fixture f(sourceRun, targetRun, QString("zone-%1").arg(index), true);
    const std::string marker = "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.invalid/owned-recovery\r\n";
    // 시험 파일의 다운로드 표식 경로만 만드는 함수.
    const auto zone = [](const std::filesystem::path& path) { return std::filesystem::path(path.wstring() + L":Zone.Identifier"); };
    if (index <= 2) {
        int checks = 0;
        require(!f.commit([&] { return ++checks >= 3; }), "Owned copy must stop after one block");
        require(std::filesystem::file_size(f.attempt.staging) == 1024 * 1024, "Genuine partial staging required");
        if (index) createFile(zone(f.attempt.staging), index == 1 ? marker : "owned different marker");
    } else {
        if (index == 3) f.faults.failAt = TransferPhase::CommitIntent;
        if (index == 4) f.faults.failAt = TransferPhase::DestinationCommitted;
        const auto copied = f.commit();
        require(bool(copied) == (index != 3 && index != 4), "Requested commit boundary missing");
        if (index == 6 || index == 7) {
            f.faults.failAt = index == 6 ? TransferPhase::SourceRemoved : TransferPhase::Completed;
            require(!f.finish() && !std::filesystem::exists(f.attempt.source), "Source-absent interrupted boundary required");
        }
        if (index == 8) {
            QFile tag(QString::fromStdWString(zone(f.attempt.destination).wstring()));
            require(tag.open(QIODevice::WriteOnly | QIODevice::Truncate) && tag.write("owned changed marker") > 0,
                "Only owned final marker may change");
        }
    }
    InspectTransferRecoveryUseCase inspect(f.history, f.journal, f.files, f.hasher);
    WindowsTransferRecoveryGuard removalGuard;
    WindowsTransferCommitRecoveryGuard commitGuard;
    ResumeInterruptedTransferUseCase resume(f.history, f.journal, f.copy, f.removal, f.files, removalGuard, commitGuard, f.hasher);
    const auto before = take(f.journal.load(f.attempt.id));
    const auto observed = take(inspect.execute(f.attempt.id));
    if (index == 2 || index == 6 || index == 7 || index == 8) {
        const bool mismatch = index == 2 || index == 8;
        require(observed.finding == (mismatch ? TransferRecoveryFinding::ChangedFile : TransferRecoveryFinding::Unavailable) &&
            !resume.verifyResume(observed) && !resume.resume(f.attempt.id), "Unsafe marker recovery must stay blocked");
        require(take(f.journal.load(f.attempt.id)).phase == before.phase, "Blocked recovery changed records");
        require(std::filesystem::exists(f.attempt.source) == mismatch, "Blocked recovery changed source presence");
        require(std::filesystem::exists(index == 2 ? f.attempt.staging : f.attempt.destination), "Blocked recovery removed owned copy");
        if (index == 2) {
            const auto opened = f.copy.openResume(before, &*f.plan.steps[0].movement);
            require(!opened && opened.error().code == "transfer.zone_changed", "Native resume must independently reject mismatched marker");
        }
        if (index == 8) {
            auto opened = take(f.removal.openApproved(before, *f.plan.steps[0].movement));
            const auto checked = opened->verify();
            require(!checked && checked.error().code == "transfer.zone_changed", "Native removal must independently reject mismatched marker");
        }
        if (!mismatch) {
            const auto guard = removalGuard.open(before, &*f.plan.steps[0].movement);
            require(!guard && guard.error().code == "recovery.zone_evidence_required", "Fresh source-absent guard must refuse missing marker proof");
        }
        return;
    }
    saved(resume.verifyResume(observed)); saved(resume.resume(f.attempt.id)); f.completed();
    QFile tag(QString::fromStdWString(zone(f.attempt.destination).wstring()));
    require(tag.open(QIODevice::ReadOnly) && tag.readAll().toStdString() == marker, "Recovered final marker bytes differ"); tag.close();
    InspectInterruptedPlanUseCase whole(f.history, f.journal, f.files, f.hasher, &f.copy);
    require(take(whole.inspectAfterResume(f.plan.id)).moved == 1, "Completed tagged result must be recognized");
    f.rollback();
    QFile restored(QString::fromStdWString(zone(f.attempt.source).wstring()));
    require(restored.open(QIODevice::ReadOnly) && restored.readAll().toStdString() == marker, "Recovered move undo lost marker");
}

// 실제 두 볼륨에서 성공·충돌·중단 재개·기록 복구의 한 시나리오를 검사하는 함수.
void scenario(const QString& sourceRun, const QString& targetRun, int index) {
    Fixture fixture(sourceRun, targetRun, QString("case-%1").arg(index));
    if (index == 1) {
        std::filesystem::create_directories(fixture.attempt.destination.parent_path());
        createFile(fixture.attempt.destination, "owned collision sentinel");
        const auto sentinel = fingerprint(fixture.attempt.destination);
        require(!fixture.commit(), "Existing final should reject transfer");
        require(sameFingerprint(sentinel, fingerprint(fixture.attempt.destination)) &&
            sameFingerprint(fixture.attempt.sourceFingerprint, fingerprint(fixture.attempt.source)), "Collision overwrote a file");
        require(take(fixture.journal.load(fixture.attempt.id)).phase != TransferPhase::Completed, "Collision falsely completed");
        return;
    }
    if (index == 2) {
        int checks = 0;
        // 한 블록을 실제 복사한 뒤 취소해 부분 파일을 남기는 함수.
        require(!fixture.commit([&] { return ++checks >= 3; }), "Partial copy cancellation should stop");
        require(std::filesystem::file_size(fixture.attempt.staging) == 1024 * 1024 &&
            sameFingerprint(fixture.attempt.sourceFingerprint, fingerprint(fixture.attempt.source)), "Real partial copy or retained source differs");
        ResumePartialCopyUseCase resume(fixture.history, fixture.journal, fixture.copy, fixture.hasher);
        require(take(resume.execute(fixture.attempt.id)).copyResume == CopyResume::Verified, "Resume whole verification missing");
        CommitResumedStagingUseCase commit(fixture.history, fixture.journal, fixture.copy, fixture.hasher);
        take(commit.execute(fixture.attempt.id));
        WindowsTransferRecoveryGuard guard;
        ResumeCommittedTransferUseCase finish(fixture.history, fixture.journal, fixture.removal, guard, fixture.hasher);
        take(finish.execute(fixture.attempt.id));
    } else if (index == 3) {
        fixture.faults.failAt = TransferPhase::DestinationCommitted;
        require(!fixture.commit(), "Commit result interruption should fail");
        require(std::filesystem::exists(fixture.attempt.source) && std::filesystem::exists(fixture.attempt.destination) &&
            !std::filesystem::exists(fixture.attempt.staging), "Unrecorded final did not retain both files");
        // 연결을 새로 열어 메모리가 아닌 SQLite 재생 증거로 복구한다.
        SqliteTransferJournal restarted(fixture.database, fixture.migration);
        WindowsTransferCommitRecoveryGuard guard;
        ReconcileTransferCommitUseCase recover(fixture.history, restarted, guard, fixture.hasher);
        take(recover.execute(fixture.attempt.id));
        WindowsTransferRecoveryGuard finalGuard;
        ResumeCommittedTransferUseCase finish(fixture.history, restarted, fixture.removal, finalGuard, fixture.hasher);
        take(finish.execute(fixture.attempt.id));
    } else {
        take(fixture.commit());
        require(std::filesystem::exists(fixture.attempt.source), "Commit alone removed original");
        if (index == 4) {
            fixture.faults.failAt = TransferPhase::SourceRemoved;
            require(!fixture.finish(), "Removal result interruption should fail");
            require(!std::filesystem::exists(fixture.attempt.source) && std::filesystem::exists(fixture.attempt.destination), "Removal interruption lost final file");
            SqliteTransferJournal restarted(fixture.database, fixture.migration);
            WindowsTransferRecoveryGuard guard;
            ReconcileTransferRecoveryUseCase recover(fixture.history, restarted, guard, fixture.hasher);
            take(recover.execute(fixture.attempt.id));
        } else take(fixture.finish());
    }
    fixture.completed();
    if (index == 5) {
        createFile(fixture.attempt.source, "owned reverse collision sentinel");
        const auto sentinel = fingerprint(fixture.attempt.source);
        const auto moved = fingerprint(fixture.attempt.destination);
        RollbackTransferUseCase undo(fixture.journal, fixture.history, fixture.copy, fixture.removal, fixture.files, fixture.hasher);
        require(!undo.execute(fixture.attempt.id, "reverse", fixture.sourceRoot / "reverse-stage.tmp"), "Reverse collision must reject");
        require(sameFingerprint(sentinel, fingerprint(fixture.attempt.source)) && sameFingerprint(moved, fingerprint(fixture.attempt.destination)),
            "Reverse collision modified an existing file");
    } else fixture.rollback();
}

// 시험 전용 목적지만 제공하고 사용자 설정 파일은 읽거나 바꾸지 않는 저장소.
class BatchSettings final : public ISettingsRepository {
public:
    UserSettings value;
    // 이번 시험의 목적지 설정을 읽는 함수.
    Result<UserSettings> load() const override { return Result<UserSettings>::success(value); }
    // 실제 사용자 설정 저장을 거부하는 함수.
    Result<void> save(const UserSettings&) override {
        return Result<void>::failure({"test.read_only", "Test settings are read-only."});
    }
};

// 실제 규칙 엔진이 해석할 고정 v3 규칙만 제공하는 저장소.
class BatchRules final : public IRuleSetRepository {
public:
    // 확장자 조건 하나와 목적지 리프를 가진 규칙을 만드는 함수.
    Result<RuleSet> load() const override {
        RuleSet rules;
        rules.id = "native-mixed"; rules.version = "native-mixed-v1"; rules.schemaVersion = 3;
        RuleNode condition;
        condition.id = "jpg"; condition.atomicCondition = ExtensionPredicate{{"jpg"}};
        RuleNode leaf;
        leaf.id = "destination"; leaf.otherwise = true;
        leaf.destination = RuleDestination{"pictures", CollisionPolicy::Abort};
        condition.children.push_back(leaf); rules.nodes.push_back(condition);
        return Result<RuleSet>::success(std::move(rules));
    }
};

// 시험 전용 SQLite에 오류 트리거를 만들거나 선택 감사 개수만 조회하는 함수.
qlonglong testSql(const QString& database, const QString& sql) {
    const auto name = QUuid::createUuid().toString();
    qlonglong result = 0;
    bool okay = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(database);
        if (db.open()) {
            QSqlQuery query(db);
            okay = query.exec(sql);
            if (okay && query.next()) result = query.value(0).toLongLong();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    require(okay, "Owned SQLite diagnostic failed");
    return result;
}

// 보존한 부분 파일의 승인·정리·감사 실패와 기록만 복구하는 경계를 시험하는 함수.
void preservedCleanupScenario(SqliteHistoryRepository& history, SqliteTransferJournal& journal,
    WindowsTransferFileInspector& files, WindowsStagedCopy& volumes, QtPlanHasher& hasher,
    const QString& database, const TransferAttempt& attempt, int scenario) {
    WindowsPreservedStagingCleanup worker;
    CleanupPreservedStagingUseCase useCase(history, journal, files, worker, hasher);
    PreservedStagingCleanupService service(useCase);
    const auto plan = take(history.loadPlan(attempt.planId));
    const auto stage = fingerprint(attempt.staging);
    const auto events = testSql(database, "SELECT COUNT(*) FROM events");
    auto approval = take(service.inspect(plan.id, attempt.id));
    require(testSql(database, "SELECT COUNT(*) FROM events") == events && sameFingerprint(stage, fingerprint(attempt.staging)),
        "Cleanup preview changed files or DB");
    require(!service.execute({approval, false}) && !take(history.preservedCleanup(plan.id, attempt.id)), "Missing explicit approval must not record intent");
    if (scenario == 0) {
        const auto& movement = *plan.steps.back().movement;
        {
            auto guarded = take(worker.openPreserved(attempt, movement, approval.stagingFingerprint, false));
            saved(guarded->verify());
            for (const auto& protectedPath : {attempt.source, attempt.staging}) {
                const auto writer = CreateFileW(protectedPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                require(writer == INVALID_HANDLE_VALUE, "Partial cleanup session must block source/staging writes");
            }
        }
        require(sameFingerprint(stage, fingerprint(attempt.staging)), "Opening and releasing cleanup session must not delete staging");
        auto wrongVolume = movement; wrongVolume.destinationVolume.guidPath = "\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\";
        require(!worker.openPreserved(attempt, wrongVolume, approval.stagingFingerprint, false), "Changed approved volume must block cleanup");
    }
    if (scenario == 1) approval.planHash = std::string(64, 'e');
    if (scenario == 2) {
        QFile source(QString::fromStdWString(attempt.source.wstring()));
        require(source.open(QIODevice::Append) && source.write("changed") == 7, "Cannot change owned cleanup source");
        source.close();
    }
    const auto backup = attempt.staging.parent_path() / "owned-partial-backup.tmp";
    if (scenario == 3) {
        std::filesystem::rename(attempt.staging, backup);
        createFile(attempt.staging, "owned replacement stage");
    }
    if (scenario == 4 || scenario == 8) {
        std::fstream partial(attempt.staging, std::ios::in | std::ios::out | std::ios::binary);
        partial.put('X'); partial.close(); require(!partial.fail(), "Cannot change owned partial prefix");
        if (scenario == 4) approval = take(service.inspect(plan.id, attempt.id)); // 새 승인이어도 원본 접두부가 아니면 거부한다.
    }
    if (scenario == 9) require(SetFileAttributesW(attempt.staging.c_str(), FILE_ATTRIBUTE_READONLY), "Cannot set owned staging readonly");
    std::optional<FileFingerprint> finalOccupant;
    if (scenario == 10) {
        createFile(attempt.destination, "owned late final occupant"); finalOccupant = fingerprint(attempt.destination);
    }
    if (scenario == 11) std::filesystem::rename(attempt.staging, backup);
    if (scenario == 5 || scenario == 6 || scenario == 7) {
        const auto type = scenario == 5 ? "preserved_cleanup_intent" : "preserved_cleanup_removed";
        testSql(database, QString("CREATE TRIGGER stop_partial_cleanup BEFORE INSERT ON events WHEN NEW.event_type='%1' BEGIN SELECT RAISE(FAIL,'owned cleanup audit failure'); END").arg(type));
    }
    const auto stageBefore = std::filesystem::exists(attempt.staging) ? std::optional<FileFingerprint>{fingerprint(attempt.staging)} : std::nullopt;
    const auto result = service.execute({approval, true});
    if (scenario == 0 || scenario == 12) {
        saved(result);
        require(!std::filesystem::exists(attempt.staging) && take(history.preservedCleanup(plan.id, attempt.id))->removed &&
            !service.execute({approval, true}), "Explicit partial cleanup must finish once");
        if (scenario == 12) {
            testSql(database, "UPDATE events SET payload_json='{}' WHERE event_type='preserved_cleanup_removed'");
            require(!history.preservedCleanup(plan.id, attempt.id) && !service.execute({approval, true}), "Corrupt completion audit must not silently succeed");
        }
    } else if (scenario == 6 || scenario == 7) {
        require(!result && !std::filesystem::exists(attempt.staging) && take(history.preservedCleanup(plan.id, attempt.id)) &&
            !take(history.preservedCleanup(plan.id, attempt.id))->removed, "Post-delete audit failure must preserve the original intent");
        testSql(database, "DROP TRIGGER stop_partial_cleanup");
        SqliteHistoryRepository reopened(database, QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql");
        CleanupPreservedStagingUseCase restarted(reopened, journal, files, worker, hasher);
        PreservedStagingCleanupService retry(restarted);
        if (scenario == 7) {
            createFile(attempt.staging, "owned later staging occupant");
            const auto replacement = fingerprint(attempt.staging);
            require(!retry.execute({approval, true}) && sameFingerprint(replacement, fingerprint(attempt.staging)) &&
                !take(reopened.preservedCleanup(plan.id, attempt.id))->removed, "Cleanup retry must not delete a later occupant");
        } else {
            saved(retry.execute({approval, true}));
            require(take(reopened.preservedCleanup(plan.id, attempt.id))->removed && !retry.execute({approval, true}), "Missing-stage retry must complete only the audit");
        }
    } else {
        require(!result && (stageBefore ? sameFingerprint(*stageBefore, fingerprint(attempt.staging)) : !std::filesystem::exists(attempt.staging)) && !take(history.preservedCleanup(plan.id, attempt.id)) &&
            testSql(database, "SELECT COUNT(*) FROM events") == events, "Rejected cleanup must preserve staging and all records");
        if (scenario == 3 || scenario == 11) require(sameFingerprint(stage, fingerprint(backup)), "Rejected cleanup lost the owned stage backup");
    }
    require(take(history.loadPlan(plan.id)).status == PlanStatus::RolledBack &&
        PlanSignature::planContents(take(history.loadPlan(plan.id))) == PlanSignature::planContents(plan) &&
        take(journal.load(attempt.id)).phase == attempt.phase && take(journal.load(attempt.id)).copyResume == attempt.copyResume,
        "Partial cleanup must not rewrite movement or transfer history");
    if (scenario != 2) require(sameFingerprint(attempt.sourceFingerprint, fingerprint(attempt.source)), "Cleanup changed the original");
    for (const auto& step : plan.steps) {
        if (finalOccupant && step.id == attempt.stepId) require(sameFingerprint(*finalOccupant, fingerprint(step.destination)), "Cleanup changed a final occupant");
        else require(!std::filesystem::exists(step.destination), "Cleanup created a final destination");
    }
    InspectInterruptedPlanUseCase reminders(history, journal, files, hasher, &volumes);
    const auto remaining = reminders.inspectAll();
    if (scenario == 12) require(!remaining, "Corrupt cleanup audit must remain an explicit inspection error, not disappear");
    else require(remaining && remaining.value().empty() == (scenario == 0 || scenario == 6), "Completed cleanup must resolve only its staging reminder");
    if (scenario == 9) require(SetFileAttributesW(attempt.staging.c_str(), FILE_ATTRIBUTE_NORMAL), "Cannot restore owned test-only readonly attribute");
}

// 실제 SQLite 기록 경계에 시험 전용 충돌만 삽입하는 저장소 대리자.
class BatchHistory final : public IHistoryRepository {
    IHistoryRepository& inner_;
public:
    std::function<void(const DomainEvent&)> beforeEvent;
    std::function<void(const PreservedCleanupRecord&)> beforeCleanup, afterCleanup;
    // 외부에서 소유한 실제 이력 저장소를 받는 함수.
    explicit BatchHistory(IHistoryRepository& inner) : inner_(inner) {}
    // 보존 등록을 실제 SQLite에서 읽는 함수.
    Result<std::vector<PreservedTransfer>> preservedTransfers(const PlanId& id) const override { return inner_.preservedTransfers(id); }
    // 보존 정리 감사를 실제 SQLite에서 읽는 함수.
    Result<std::optional<PreservedCleanupRecord>> preservedCleanup(const PlanId& id, const std::string& attempt) const override { return inner_.preservedCleanup(id, attempt); }
    // 실제 정리 감사의 직전·직후 경계에서만 시험 프로세스를 멈추는 함수.
    Result<void> recordPreservedCleanup(const PreservedCleanupRecord& record) override {
        if (beforeCleanup) beforeCleanup(record);
        const auto result = inner_.recordPreservedCleanup(record);
        if (result && afterCleanup) afterCleanup(record);
        return result;
    }
    // 계획 저장을 실제 SQLite로 전달하는 함수.
    Result<void> savePlan(const OperationPlan& plan) override { return inner_.savePlan(plan); }
    // 승인 저장을 실제 SQLite로 전달하는 함수.
    Result<void> saveApproval(const ApprovalRecord& approval) override { return inner_.saveApproval(approval); }
    // 지정한 기록 경계에서만 시험 사건을 일으키는 함수.
    Result<void> appendEvent(const DomainEvent& event) override {
        if (beforeEvent) beforeEvent(event);
        return inner_.appendEvent(event);
    }
    // 미완료 계획을 실제 SQLite에서 읽는 함수.
    Result<std::vector<OperationPlan>> incompletePlans() const override { return inner_.incompletePlans(); }
    // 저장 계획 하나를 실제 SQLite에서 읽는 함수.
    Result<OperationPlan> loadPlan(const PlanId& id) const override { return inner_.loadPlan(id); }
    // 최근 계획을 실제 SQLite에서 읽는 함수.
    Result<std::vector<PlanHistoryEntry>> recentPlans(int limit) const override { return inner_.recentPlans(limit); }
    // 복구 직전 상태를 실제 SQLite에서 읽는 함수.
    Result<PlanStatus> interruptedStatus(const PlanId& id) const override { return inner_.interruptedStatus(id); }
};

// 정상 역전송을 유지하고 마지막 파일의 실제 1MiB 복사 뒤에만 멈추는 시험 대리자.
class CrashReverse final : public IRollbackTransferStepExecutor {
    RollbackTransferUseCase& inner_;
    const ITransferJournal& journal_;
public:
    StepId target;
    // 외부 소유 역전송과 조회 포트를 빌려 받는 함수.
    CrashReverse(RollbackTransferUseCase& inner, const ITransferJournal& journal) : inner_(inner), journal_(journal) {}
    // 기존 읽기 전용 역전송 사전 검사를 전달하는 함수.
    Result<void> verifyRollback(const PlanId& plan, const StepId& step) override { return inner_.verifyRollback(plan, step); }
    // 지정한 단계의 역전송 중 자기 프로세스만 부모의 종료를 기다리는 함수.
    Result<void> rollback(const PlanId& plan, const StepId& step, const std::string& id, const std::filesystem::path& staging) override {
        if (step != target) return inner_.rollback(plan, step, id, staging);
        const auto attempts = take(journal_.forStep(step));
        require(attempts.size() == 1 && attempts[0].direction == TransferDirection::Forward && attempts[0].phase == TransferPhase::Completed,
            "Owned reverse crash needs a completed forward attempt");
        const auto result = inner_.execute(attempts[0].id, id, staging, [&] {
            if (std::filesystem::exists(staging) && std::filesystem::file_size(staging) >= 1024 * 1024) checkpoint("reverse-mid-copy");
            return false;
        });
        return result ? Result<void>::success() : Result<void>::failure(result.error());
    }
};

// 실제 두 볼륨에서 규칙 미리보기부터 승인·혼합 실행·보상·되돌리기를 검사하는 함수.
void batchScenario(const QString& sourceRun, const QString& targetRun, int index, const QString& crashAt = {}) {
    const bool cleanupCrash = crashAt.startsWith("cleanup-");
    const bool reverseCrash = crashAt == "reverse-mid-copy";
    const bool recoveringBatch = index >= 5 && index < 15;
    const bool recoveringTransfer = index >= 15;
    const auto name = QString("case-%1").arg(index);
    const std::filesystem::path cRoot(QDir(sourceRun).filePath(name).toStdWString());
    const std::filesystem::path dRoot(QDir(targetRun).filePath(name).toStdWString());
    require(std::filesystem::create_directory(cRoot) && std::filesystem::create_directory(dRoot), "Cannot create owned batch folders");
    const bool renameFirst = index == 1 || index == 2;
    const bool copyLast = index == 4 || index == 14 || index == 21 || (index >= 26 && index != 30);
    const std::vector<std::filesystem::path> selected{
        (renameFirst ? dRoot : cRoot) / L"첫째.jpg", (renameFirst ? cRoot : dRoot) / L"둘째.jpg",
        (copyLast ? cRoot : dRoot) / L"마지막.jpg"};
    const size_t interruptedIndex = index == 21 || (index >= 26 && index != 30) ? 2 : 0;
    for (size_t i = 0; i < selected.size(); ++i) {
        std::string contents = "owned batch contents " + std::to_string(i);
        if (recoveringTransfer && i == interruptedIndex) {
            contents.resize(4 * 1024 * 1024 + 37);
            for (size_t j = 0; j < contents.size(); ++j) contents[j] = static_cast<char>((j * 31 + j / 257) % 256);
        }
        createFile(selected[i], contents);
    }
    const std::filesystem::path appData = dRoot / "app-data";
    const auto database = QString::fromStdWString((appData / "history.sqlite").wstring());
    const auto migration = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
    BatchSettings settings;
    settings.value.homeRoot = dRoot / "organized";
    BatchRules rules;
    FilesystemScanner scanner;
    RuleEngine engine;
    Planner planner(engine, appData, CrossVolumeDrafts::Allow);
    Planner legacyPlanner(engine, appData);
    QtPlanHasher hasher;
    WindowsStagedCopy copy;
    WindowsTransferSourceRemoval removal;
    WindowsTransferFileInspector files;
    SqliteHistoryRepository history(database, migration);
    SqliteTransferJournal journal(database, migration);
    BatchHistory hooks(history);
    CreatePreviewUseCase legacyPreview(settings, rules, scanner, legacyPlanner, &hasher);
    CreatePreviewUseCase missingVolumes(settings, rules, scanner, planner, &hasher);
    const CreatePreviewRequest request{take(rules.load()).version, selected};
    require(!legacyPreview.execute(request), "Existing app planning must remain rename-only until opted in");
    const auto uncontracted = missingVolumes.execute(request);
    require(!uncontracted && uncontracted.error().code == "plan.volume_contract_required", "Missing volume wiring must not emit a cross-drive v1 preview");
    CreatePreviewUseCase preview(settings, rules, scanner, planner, &hasher, &copy);
    const auto draft = take(preview.execute(request));
    require(draft.formatVersion == 2 && draft.steps.size() == 3, "Native preview must prepare all three contracts");
    for (size_t i = 0; i < selected.size(); ++i) {
        require(draft.steps[i].source == selected[i] && draft.steps[i].movement->strategy ==
            (((renameFirst ? i == 1 : i == 0) || (copyLast && i == 2)) ? MoveStrategy::VerifiedCopy : MoveStrategy::Rename), "Real mixed movement order differs");
        require(std::filesystem::exists(selected[i]) && !std::filesystem::exists(draft.steps[i].destination), "Preview must not move files");
    }
    FreezePreviewUseCase freezer(rules, scanner, planner, hasher, history, &copy);
    auto downgraded = draft;
    downgraded.formatVersion = 1;
    for (auto& step : downgraded.steps) step.movement.reset();
    require(!freezer.execute(downgraded, selected, settings.value), "Uncontracted cross-drive freeze must fail");
    const auto frozen = take(freezer.execute(draft, selected, settings.value));
    VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history, &copy);
    ApprovePlanUseCase approve(verifier, history);
    const auto approval = take(approve.execute(frozen.id, selected, settings.value));
    const auto plan = take(history.loadPlan(frozen.id));
    ExecuteTransferStepUseCase forward(copy, removal, journal, history, hasher);
    FaultJournal interruptedJournal(journal);
    InterruptedCopyStep interruptedForward(forward, history, copy, removal, interruptedJournal, hasher);
    interruptedForward.target = plan.steps[interruptedIndex].id;
    interruptedForward.partial = index == 15 || index >= 21;
    if (!crashAt.isEmpty()) {
        interruptedForward.crashAt = cleanupCrash || reverseCrash ? QString{} : crashAt;
        interruptedForward.partial = cleanupCrash || crashAt == "mid-copy";
    }
    if (index == 16) interruptedJournal.throwAt = TransferPhase::CommitIntent;
    if (index == 17) interruptedJournal.throwAt = TransferPhase::DestinationCommitted;
    if (index == 18) interruptedJournal.throwAt = TransferPhase::SourceRemoveIntent;
    if (index == 19) interruptedJournal.throwAt = TransferPhase::SourceRemoved;
    if (index == 20) interruptedJournal.throwBeforeCompletion = true;
    RollbackTransferUseCase reverse(journal, history, copy, removal, files, hasher);
    CrashReverse crashReverse(reverse, journal);
    crashReverse.target = plan.steps.back().id;
    SafeExecutor executor(settings, verifier, scanner, hasher, hooks, appData, &copy,
        recoveringTransfer && !reverseCrash ? static_cast<ITransferStepExecutor*>(&interruptedForward) : &forward,
        reverseCrash ? static_cast<IRollbackTransferStepExecutor*>(&crashReverse) : &reverse);
    SafeExecutor missingReverse(settings, verifier, scanner, hasher, history, appData, &copy, &forward);
    require(!missingReverse.execute(plan, approval, selected) && take(history.loadPlan(plan.id)).status == PlanStatus::Approved,
        "Batch without compensation capability must stop before starting");
    std::optional<FileFingerprint> sentinel;
    size_t started = 0;
    // 혼합 배치가 중간에 전체 완료되지 않는지 검사하고 지정한 마지막 단계에만 충돌을 넣는 함수.
    hooks.beforeEvent = [&](const DomainEvent& event) {
        const auto* changed = std::get_if<StepStatusChanged>(&event);
        if (changed && changed->status == StepStatus::IntentRecorded) {
            const auto running = take(history.loadPlan(plan.id));
            require(running.status == PlanStatus::Executing && changed->stepId == plan.steps[started].id,
                "Mixed batch must stay executing and preserve approved order until every step finishes");
            for (size_t i = 0; i < started; ++i)
                require(running.steps[i].status == StepStatus::Applied, "Previous mixed step is not durably applied");
            if (started == 2) {
                InspectInterruptedPlanUseCase inspect(history, journal, files, hasher, &copy);
                const auto report = take(inspect.execute(plan.id));
                require(report.moved == 2 && report.pending == 1 && report.needsAttention == 0 &&
                    report.resume.readiness == RecoveryReadiness::NeedsRevalidation && report.rollback.readiness == RecoveryReadiness::NeedsRevalidation,
                    "Read-only native mixed inspection must report two moved and one pending, not execute a recovery");
                const auto afterInspection = take(history.loadPlan(plan.id));
                require(afterInspection.status == running.status && PlanSignature::planContents(afterInspection) == PlanSignature::planContents(running),
                    "Read-only mixed inspection changed operation records");
                if (crashAt == "before-last") checkpoint(crashAt);
                if (recoveringBatch) throw std::runtime_error("owned-batch-stop");
            }
            ++started;
            if (index == 2 && changed->stepId == plan.steps[2].id) {
                createFile(plan.steps[2].destination, "owned collision sentinel");
                sentinel = fingerprint(plan.steps[2].destination);
            }
        }
    };
    auto result = Result<void>::success();
    bool stopped = false;
    try { result = executor.execute(plan, approval, selected); }
    catch (const std::runtime_error& error) {
        if ((!recoveringBatch || std::string(error.what()) != "owned-batch-stop") &&
            (!recoveringTransfer || std::string(error.what()) != "owned-transfer-stop")) throw;
        stopped = true;
    }
    hooks.beforeEvent = {};
    if (reverseCrash) {
        saved(result);
        saved(executor.rollback(plan.id));
        throw std::runtime_error("Reverse worker completed without requested checkpoint");
    }
    require(crashAt.isEmpty() || cleanupCrash, "Mixed worker completed without reaching its requested checkpoint");
    if (recoveringTransfer) {
        require(interruptedForward.interruptedOnce && (stopped || !result), "Owned copy should stop at its requested boundary");
        const auto attempts = take(journal.forStep(plan.steps[interruptedIndex].id));
        require(attempts.size() == 1, "Exactly one owned unresolved forward attempt required");
        const auto attempt = attempts[0];
        const auto before = take(history.loadPlan(plan.id));
        const auto beforeEvents = testSql(database, "SELECT COUNT(*) FROM events");
        std::optional<FileFingerprint> stageBefore;
        if (std::filesystem::exists(attempt.staging)) stageBefore = fingerprint(attempt.staging);
        InspectInterruptedPlanUseCase inspector(history, journal, files, hasher, &copy);
        WindowsTransferRecoveryGuard removalGuard;
        WindowsTransferCommitRecoveryGuard commitGuard;
        ResumeInterruptedTransferUseCase resolver(history, journal, copy, removal, files, removalGuard, commitGuard, hasher);
        RecoverInterruptedPlanUseCase recovery(inspector, history, settings, rules, hasher, executor, &resolver);
        InterruptedOperationRecoveryService service(recovery);
        const bool preservingUndo = index == 24 || index >= 26;
        RecoveryRequest recoveryRequest{plan.id, plan.planHash, preservingUndo ? RecoveryAction::Rollback : RecoveryAction::Resume};
        std::optional<FileFingerprint> collisionBefore;
        if (index == 22) {
            createFile(attempt.destination, "owned collision sentinel");
            collisionBefore = fingerprint(attempt.destination);
        }
        if (index == 23) testSql(database, "CREATE TRIGGER stop_transfer_recovery_choice BEFORE INSERT ON events WHEN NEW.event_type='recovery_choice_recorded' BEGIN SELECT RAISE(FAIL,'owned audit failure'); END");
        if (index == 25) {
            QFile changed(QString::fromStdWString(attempt.staging.wstring()));
            require(changed.open(QIODevice::Append) && changed.write("!") == 1, "Cannot corrupt owned partial prefix");
            changed.close(); stageBefore = fingerprint(attempt.staging);
        }
        if (index == 27) testSql(database, "CREATE TRIGGER stop_preserved_undo BEFORE INSERT ON events WHEN NEW.event_type='recovery_choice_recorded' BEGIN SELECT RAISE(FAIL,'owned undo audit failure'); END");
        if (index == 28) {
            QFile changed(QString::fromStdWString(attempt.source.wstring()));
            require(changed.open(QIODevice::Append) && changed.write("changed") == 7, "Cannot change owned partial source"); changed.close();
        }
        std::optional<FileFingerprint> originalStage;
        std::filesystem::path stageBackup;
        if (index == 29) {
            originalStage = stageBefore; stageBackup = attempt.staging; stageBackup += ".owned-backup";
            require(!std::filesystem::exists(stageBackup), "Owned backup must be absent");
            std::filesystem::rename(attempt.staging, stageBackup); createFile(attempt.staging, "replacement stage");
            stageBefore = fingerprint(attempt.staging);
        }
        std::optional<FileFingerprint> lateOccupant;
        if (index == 31) {
            // 역순 이름 변경 뒤 원래 복사 위치에 새 파일을 놓아 두 번째 복원을 막는 함수.
            hooks.beforeEvent = [&](const DomainEvent& event) {
                const auto* changed = std::get_if<StepStatusChanged>(&event);
                if (changed && changed->stepId == plan.steps[1].id && changed->status == StepStatus::RolledBack) {
                    createFile(plan.steps[0].source, "owned late occupant"); lateOccupant = fingerprint(plan.steps[0].source);
                }
            };
        }
        const auto recovered = service.execute(recoveryRequest);
        hooks.beforeEvent = {};
        const auto after = take(history.loadPlan(plan.id));
        if (preservingUndo) {
            if (index == 27 || index == 28 || index == 29) {
                require(!recovered && after.status == before.status && PlanSignature::planContents(after) == PlanSignature::planContents(before) &&
                    testSql(database, "SELECT COUNT(*) FROM events") == beforeEvents && take(history.preservedTransfers(plan.id)).empty(),
                    "Audit failure or unsafe partial evidence must not start undo or register preservation");
                require(stageBefore && sameFingerprint(*stageBefore, fingerprint(attempt.staging)), "Rejected undo changed staging");
                if (index == 29) require(originalStage && sameFingerprint(*originalStage, fingerprint(stageBackup)), "Rejected undo lost the original stage backup");
            } else {
                if (index == 31) {
                    require(!recovered && after.status == PlanStatus::NeedsRecovery && after.steps[1].status == StepStatus::RolledBack &&
                        lateOccupant && sameFingerprint(*lateOccupant, fingerprint(plan.steps[0].source)) &&
                        TransferValidation::sameContent(plan.steps[0].fingerprint, fingerprint(plan.steps[0].destination)),
                        "Late undo collision must preserve the occupant, moved copy and already-restored step");
                } else {
                    saved(recovered);
                    require(after.status == PlanStatus::RolledBack && after.planHash == plan.planHash, "Partial-preserving undo must finish the movement portion");
                    for (const auto& step : plan.steps) require(!std::filesystem::exists(step.destination) &&
                        TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)), "Partial-preserving undo did not return original contents");
                    require(!service.execute(recoveryRequest), "Finished preserved undo must not repeat");
                }
                require(stageBefore && sameFingerprint(*stageBefore, fingerprint(attempt.staging)) &&
                    sameFingerprint(attempt.sourceFingerprint, fingerprint(attempt.source)) &&
                    take(journal.load(attempt.id)).phase == attempt.phase && take(journal.load(attempt.id)).copyResume == attempt.copyResume,
                    "Undo must neither finish the partial copy nor delete/change its source or stage");
                SqliteHistoryRepository restarted(database, migration);
                const auto retained = take(restarted.preservedTransfers(plan.id));
                require(retained.size() == 1 && retained[0].attemptId == attempt.id && retained[0].staging == attempt.staging &&
                    testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='recovery_choice_recorded'") == 1,
                    "Preserved staging registration must survive reopening SQLite under one explicit choice");
                InspectInterruptedPlanUseCase refreshed(restarted, journal, files, hasher, &copy);
                const auto reminders = take(refreshed.inspectAll());
                require(reminders.size() == 1 && reminders[0].preserved == 1 && reminders[0].resume.readiness == RecoveryReadiness::Blocked,
                    "Preserved stage reminder must remain visible even after movement undo finishes");
                if (cleanupCrash) {
                    WindowsPreservedStagingCleanup cleanupWorker;
                    CleanupPreservedStagingUseCase cleanup(hooks, journal, files, cleanupWorker, hasher);
                    PreservedStagingCleanupService cleanupService(cleanup);
                    // 실제 삭제 이전의 저장된 의도 경계에 도착한 뒤 종료를 기다리는 함수.
                    hooks.afterCleanup = [&](const PreservedCleanupRecord& record) {
                        if (!record.removed && crashAt == "cleanup-intent") checkpoint(crashAt);
                    };
                    // 실제 삭제 이후 완료 감사 호출 직전에서 종료를 기다리는 함수.
                    hooks.beforeCleanup = [&](const PreservedCleanupRecord& record) {
                        if (record.removed) checkpoint(crashAt);
                    };
                    saved(cleanupService.execute({take(cleanupService.inspect(plan.id, attempt.id)), true}));
                    throw std::runtime_error("Cleanup worker completed without requested checkpoint");
                }
                if (index >= 39) preservedCleanupScenario(history, journal, files, copy, hasher, database, attempt, index - 39);
            }
            return;
        }
        if (index >= 22) {
            require(!recovered, "Unsafe partial recovery should stop");
            if (index == 25) {
                require(after.status == PlanStatus::Executing && testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='recovery_choice_recorded'") == 1 &&
                    take(journal.load(attempt.id)).copyResume == CopyResume::None, "Protected prefix failure must preserve the audit without recording a successful resume");
                for (const auto& step : plan.steps) require(std::filesystem::exists(step.source) && !std::filesystem::exists(step.destination) &&
                    TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)), "Prefix failure must not move any remaining source");
            } else require(after.status == before.status && PlanSignature::planContents(after) == PlanSignature::planContents(before) &&
                testSql(database, "SELECT COUNT(*) FROM events") == beforeEvents, "Blocked or audit-failed partial recovery changed operation records");
            require(stageBefore && sameFingerprint(*stageBefore, fingerprint(attempt.staging)) &&
                sameFingerprint(attempt.sourceFingerprint, fingerprint(attempt.source)), "Blocked partial recovery changed original or staging file");
            if (index == 22) require(collisionBefore && sameFingerprint(*collisionBefore, fingerprint(attempt.destination)), "Collision sentinel was changed");
            return;
        }
        saved(recovered);
        require(after.status == PlanStatus::Completed && after.planHash == plan.planHash &&
            testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='recovery_choice_recorded'") == 1, "Transfer resolution must complete under exactly one explicit choice");
        const auto completed = take(journal.load(attempt.id));
        require(completed.phase == TransferPhase::Completed && take(journal.forStep(attempt.stepId)).size() == 1 &&
            !std::filesystem::exists(attempt.staging), "Resolver must finish the same attempt, not silently restart a new copy");
        if (interruptedForward.partial || index == 16) require(completed.copyResume == CopyResume::Verified, "Resume did not record full staged verification");
        for (const auto& step : plan.steps) require(!std::filesystem::exists(step.source) &&
            TransferValidation::sameContent(step.fingerprint, fingerprint(step.destination)), "Resolved whole operation lost original contents");
        require(!service.execute(recoveryRequest), "Completed resolved operation must not repeat");
        saved(executor.rollback(plan.id));
        for (const auto& step : plan.steps) require(!std::filesystem::exists(step.destination) &&
            TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)), "Resolved whole operation did not support normal undo");
        return;
    }
    if (recoveringBatch) {
        require(stopped, "Owned batch should stop after two moves and before the third intent");
        if (index == 5 || index == 6 || index == 10 || index == 14) {
            for (const auto& step : plan.steps) saved(history.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::NeedsRecovery}));
            saved(history.appendEvent(PlanStatusChanged{plan.id, PlanStatus::NeedsRecovery}));
        }
        if (index == 13) {
            saved(history.appendEvent(PlanStatusChanged{plan.id, PlanStatus::RollingBack}));
            saved(history.appendEvent(StepStatusChanged{plan.id, plan.steps[1].id, StepStatus::RollbackIntentRecorded}));
            saved(WindowsFileMover::moveNoReplace(plan.steps[1].destination, plan.steps[1].source));
            saved(history.appendEvent(StepStatusChanged{plan.id, plan.steps[1].id, StepStatus::RolledBack}));
            saved(reverse.rollback(plan.id, plan.steps[0].id, "owned-reverse", plan.steps[0].source.parent_path() / "owned-reverse.tmp"));
            require(take(history.loadPlan(plan.id)).status == PlanStatus::RollingBack, "Never-started step should leave interrupted undo completion pending");
        }
        const auto before = take(history.loadPlan(plan.id));
        const auto beforeEvents = testSql(database, "SELECT COUNT(*) FROM events");
        InspectInterruptedPlanUseCase inspector(history, journal, files, hasher, &copy);
        RecoverInterruptedPlanUseCase recovery(inspector, history, settings, rules, hasher, index == 12 ? static_cast<IRecoveredPlanRunner&>(missingReverse) : executor);
        InterruptedOperationRecoveryService service(recovery);
        RecoveryRequest recoveryRequest{plan.id, plan.planHash, (index == 6 || index == 13) ? RecoveryAction::Rollback : index == 7 ? RecoveryAction::Later : RecoveryAction::Resume};
        if (index == 8) {
            QFile changed(QString::fromStdWString(selected[2].wstring()));
            require(changed.open(QIODevice::Append) && changed.write("changed") == 7, "Cannot change owned pending source");
            changed.close();
        }
        if (index == 9) settings.value.homeRoot = dRoot / "different-home";
        if (index == 10) testSql(database, "CREATE TRIGGER stop_recovery_audit BEFORE INSERT ON events WHEN NEW.event_type='recovery_choice_recorded' BEGIN SELECT RAISE(FAIL,'owned audit failure'); END");
        if (index == 11) recoveryRequest.planHash = std::string(64, 'e');
        const auto recovered = service.execute(recoveryRequest);
        const auto after = take(history.loadPlan(plan.id));
        if (index == 5 || index == 14) {
            saved(recovered);
            require(after.status == PlanStatus::Completed && after.planHash == plan.planHash, "Explicit resume must finish the same approved batch");
            for (const auto& step : plan.steps) require(!std::filesystem::exists(step.source) &&
                TransferValidation::sameContent(step.fingerprint, fingerprint(step.destination)), "Resume failed to preserve original content");
            require(take(journal.forStep(plan.steps[0].id)).size() == 1, "Resume must not create a second copy for the already completed step");
            require(!service.execute(recoveryRequest), "Completed recovery must not execute twice");
            saved(executor.rollback(plan.id));
        } else if (index == 6 || index == 13) {
            saved(recovered);
            require(after.status == PlanStatus::RolledBack && after.steps[0].status == StepStatus::RolledBack &&
                after.steps[1].status == StepStatus::RolledBack && after.steps[2].status == StepStatus::Planned,
                "Explicit undo must restore moved steps and leave never-started source untouched");
            require(!service.execute(recoveryRequest), "Completed undo must not repeat");
        } else {
            require(index == 7 ? recovered.hasValue() : !recovered, "Deferred/blocked recovery result differs");
            require(after.status == before.status && PlanSignature::planContents(after) == PlanSignature::planContents(before) &&
                testSql(database, "SELECT COUNT(*) FROM events") == beforeEvents, "Later or blocked/audit-failed choice changed recovery records");
            for (size_t i = 0; i < 2; ++i) require(!std::filesystem::exists(plan.steps[i].source) &&
                TransferValidation::sameContent(plan.steps[i].fingerprint, fingerprint(plan.steps[i].destination)), "Blocked choice changed a moved file");
            require(std::filesystem::exists(selected[2]) && !std::filesystem::exists(plan.steps[2].destination), "Blocked choice moved the pending file");
        }
        if (index == 5 || index == 6 || index == 13 || index == 14) {
            require(testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='recovery_choice_recorded'") == 1,
                "One explicit successful choice must have exactly one durable audit record");
            for (const auto& step : plan.steps) require(std::filesystem::exists(step.source) && !std::filesystem::exists(step.destination) &&
                TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)), "Recovered batch did not return all originals");
        }
        return;
    }
    const auto moved = take(history.loadPlan(plan.id));
    require(moved.planHash == plan.planHash, "Execution must preserve the exact approval hash");
    if (index == 2) {
        require(!result && result.error().code == "execute.batch_rolled_back" && moved.status == PlanStatus::Failed &&
            moved.steps[0].status == StepStatus::RolledBack && moved.steps[1].status == StepStatus::RolledBack && moved.steps[2].status == StepStatus::Failed,
            "Certain collision must compensate earlier rename and native cross-volume copy");
        for (size_t i = 0; i < selected.size(); ++i)
            require(TransferValidation::sameContent(plan.steps[i].fingerprint, fingerprint(selected[i])), "Compensation must restore every original");
        require(!std::filesystem::exists(plan.steps[0].destination) && !std::filesystem::exists(plan.steps[1].destination) &&
            sentinel && sameFingerprint(*sentinel, fingerprint(plan.steps[2].destination)), "Compensation must preserve the collision occupant");
    } else {
        saved(result);
        require(moved.status == PlanStatus::Completed, "Mixed batch did not complete");
        for (size_t i = 0; i < selected.size(); ++i) {
            require(moved.steps[i].status == StepStatus::Applied && !std::filesystem::exists(selected[i]) &&
                TransferValidation::sameContent(plan.steps[i].fingerprint, fingerprint(plan.steps[i].destination)), "Mixed applied file evidence differs");
            if (plan.steps[i].movement->strategy == MoveStrategy::Rename)
                require(sameFingerprint(plan.steps[i].fingerprint, fingerprint(plan.steps[i].destination)), "Rename must preserve the original file object");
        }
        if (index == 3) {
            createFile(selected[0], "owned reverse collision sentinel");
            sentinel = fingerprint(selected[0]);
            require(!executor.rollback(plan.id) && take(history.loadPlan(plan.id)).status == PlanStatus::Completed,
                "Undo preflight must reject collision before reversing any step");
            for (const auto& step : plan.steps)
                require(TransferValidation::sameContent(step.fingerprint, fingerprint(step.destination)), "Rejected batch undo changed a moved file");
            require(sameFingerprint(*sentinel, fingerprint(selected[0])), "Rejected undo overwrote the occupant");
        } else {
            saved(executor.rollback(plan.id));
            const auto restored = take(history.loadPlan(plan.id));
            require(restored.status == PlanStatus::RolledBack && restored.planHash == plan.planHash, "Mixed undo status or signature differs");
            for (size_t i = 0; i < selected.size(); ++i)
                require(restored.steps[i].status == StepStatus::RolledBack && !std::filesystem::exists(plan.steps[i].destination) &&
                    TransferValidation::sameContent(plan.steps[i].fingerprint, fingerprint(selected[i])), "Mixed undo must restore every original");
        }
    }
    for (const auto& step : plan.steps) {
        const auto attempts = take(journal.forStep(step.id));
        const size_t expected = step.movement->strategy == MoveStrategy::Rename ? 0 : (index == 3 ? 1 : 2);
        require(attempts.size() == expected, "Only copy steps should have separate forward/reverse transfer records");
        for (const auto& attempt : attempts) require(attempt.phase == TransferPhase::Completed, "Finished copy evidence is incomplete");
    }
}

// 혼합 강제 종료 자식이 읽을 DB와 승인 경로를 새 시험 자료로 한정하는 함수.
OperationPlan ownedMixedPlan(SqliteHistoryRepository& history, const QString& database) {
    const QFileInfo db(database);
    const QDir scenario = QFileInfo(db.dir().absolutePath()).dir();
    require(db.isAbsolute() && db.isFile() && db.fileName() == "history.sqlite" && db.dir().dirName() == "app-data" &&
        scenario.dirName().startsWith("case-") && QFileInfo(scenario.absolutePath()).dir().dirName().startsWith("NookBit-cross-target-"),
        "Owned mixed scenario DB required");
    const auto entries = take(history.recentPlans(2));
    require(entries.size() == 1, "Exactly one owned mixed plan required");
    const auto plan = take(history.loadPlan(entries[0].id));
    QtPlanHasher hasher;
    require(plan.ruleSetVersion == "native-mixed-v1" && plan.formatVersion == 2 && plan.steps.size() == 3 &&
        plan.planHash == hasher.sha256(PlanSignature::approvalPayload(plan)), "Owned signed mixed plan required");
    const std::filesystem::path dRoot(scenario.absolutePath().toStdWString());
    const auto cRoot = plan.steps[0].source.parent_path();
    require(cRoot.filename() == dRoot.filename() && QFileInfo(QString::fromStdWString(cRoot.wstring())).dir().dirName().startsWith("NookBit-cross-source-") &&
        std::filesystem::is_directory(cRoot), "Owned source run required");
    const std::vector<std::filesystem::path> sources{cRoot / L"첫째.jpg", dRoot / L"둘째.jpg", cRoot / L"마지막.jpg"};
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& step = plan.steps[i];
        std::string contents = "owned batch contents " + std::to_string(i);
        if (i == 2) {
            contents.resize(4 * 1024 * 1024 + 37);
            for (size_t j = 0; j < contents.size(); ++j) contents[j] = static_cast<char>((j * 31 + j / 257) % 256);
        }
        require(step.source == sources[i] && step.destination == dRoot / "organized" / "pictures" / sources[i].filename() &&
            step.fingerprint.size == contents.size() && step.fingerprint.sha256 == std::optional<std::string>{hasher.sha256(contents)} &&
            step.movement && step.movement->strategy == (i == 1 ? MoveStrategy::Rename : MoveStrategy::VerifiedCopy),
            "Unexpected mixed paths, contents or movement contracts");
    }
    return plan;
}

// 시험 전용 혼합 계획을 새 프로세스에서 진단하고 재개 또는 복원하는 함수.
int mixedRecoveryChild(const QString& database, const QString& choice) {
    const auto parts = choice.split('|');
    require(parts.size() == 2 && QStringList{"before-last", "mid-copy", "after-rename", "after-remove", "before-complete"}.contains(parts[0]) &&
        QStringList{"resume", "undo"}.contains(parts[1]), "Unknown owned mixed recovery choice");
    // DB가 존재하고 시험 루트 안인지 확인한 다음에만 저장소를 연다.
    const QFileInfo db(database);
    require(db.isAbsolute() && db.isFile() && db.fileName() == "history.sqlite" && db.dir().dirName() == "app-data" &&
        QFileInfo(QFileInfo(db.dir().absolutePath()).dir().absolutePath()).dir().dirName().startsWith("NookBit-cross-target-"),
        "Existing owned mixed DB required");
    OperationMutex lock;
    saved(lock.acquire());
    const auto migration = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
    SqliteHistoryRepository history(database, migration);
    SqliteTransferJournal journal(database, migration);
    const auto plan = ownedMixedPlan(history, database);
    require(plan.status == PlanStatus::Executing && plan.steps[0].status == StepStatus::Applied && plan.steps[1].status == StepStatus::Applied &&
        plan.steps[2].status == (parts[0] == "before-last" ? StepStatus::Planned : StepStatus::IntentRecorded), "Killed mixed durable step states differ");
    const auto appData = std::filesystem::path(db.dir().absolutePath().toStdWString());
    BatchSettings settings;
    settings.value.homeRoot = appData.parent_path() / "organized";
    BatchRules rules;
    FilesystemScanner scanner;
    QtPlanHasher hasher;
    WindowsStagedCopy copy;
    WindowsTransferSourceRemoval removal;
    WindowsTransferFileInspector files;
    RuleEngine engine;
    Planner planner(engine, appData, CrossVolumeDrafts::Allow);
    VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history, &copy);
    ExecuteTransferStepUseCase forward(copy, removal, journal, history, hasher);
    RollbackTransferUseCase reverse(journal, history, copy, removal, files, hasher);
    SafeExecutor executor(settings, verifier, scanner, hasher, history, appData, &copy, &forward, &reverse);
    InspectInterruptedPlanUseCase inspector(history, journal, files, hasher, &copy);
    WindowsTransferRecoveryGuard removalGuard;
    WindowsTransferCommitRecoveryGuard commitGuard;
    ResumeInterruptedTransferUseCase resolver(history, journal, copy, removal, files, removalGuard, commitGuard, hasher);
    RecoverInterruptedPlanUseCase recovery(inspector, history, settings, rules, hasher, executor, &resolver);
    InterruptedOperationRecoveryService service(recovery);
    std::vector<std::pair<std::filesystem::path, std::optional<FileFingerprint>>> observations;
    for (const auto& step : plan.steps) {
        for (const auto& path : {step.source, step.destination})
            observations.emplace_back(path, std::filesystem::exists(path) ? std::optional<FileFingerprint>{fingerprint(path)} : std::nullopt);
        for (const auto& attempt : take(journal.forStep(step.id)))
            observations.emplace_back(attempt.staging, std::filesystem::exists(attempt.staging) ? std::optional<FileFingerprint>{fingerprint(attempt.staging)} : std::nullopt);
    }
    const auto events = testSql(database, "SELECT COUNT(*) FROM events");
    const auto report = take(inspector.execute(plan.id));
    require(report.planIntact && take(inspector.inspectAll()).size() == 1 &&
        testSql(database, "SELECT COUNT(*) FROM events") == events, "Startup mixed inspection must be read-only");
    saved(service.execute({plan.id, plan.planHash, RecoveryAction::Later}));
    require(testSql(database, "SELECT COUNT(*) FROM events") == events &&
        PlanSignature::planContents(take(history.loadPlan(plan.id))) == PlanSignature::planContents(plan), "Later changed killed mixed records");
    for (const auto& [path, before] : observations)
        require(before ? std::filesystem::exists(path) && sameFingerprint(*before, fingerprint(path)) : !std::filesystem::exists(path),
            "Inspection or Later changed killed mixed files");
    const auto lastAttempts = take(journal.forStep(plan.steps[2].id));
    std::optional<FileFingerprint> partial;
    if (parts[0] == "before-last") require(lastAttempts.empty() && report.moved == 2 && report.pending == 1, "Never-started copy should have no attempt");
    else {
        require(lastAttempts.size() == 1, "Killed last copy needs exactly one attempt");
        const auto& attempt = lastAttempts[0];
        const auto expected = parts[0] == "mid-copy" ? TransferPhase::StagingCreated : parts[0] == "after-rename" ? TransferPhase::CommitIntent :
            parts[0] == "after-remove" ? TransferPhase::SourceRemoveIntent : TransferPhase::SourceRemoved;
        require(attempt.phase == expected, "Killed last copy journal boundary differs");
        if (parts[0] == "mid-copy") {
            partial = fingerprint(attempt.staging);
            require(partial->size == 1024 * 1024 && sameFingerprint(plan.steps[2].fingerprint, fingerprint(plan.steps[2].source)), "Killed mixed partial bytes differ");
        }
    }
    const bool undo = parts[1] == "undo";
    const RecoveryRequest request{plan.id, plan.planHash, undo ? RecoveryAction::Rollback : RecoveryAction::Resume};
    saved(service.execute(request));
    const auto after = take(history.loadPlan(plan.id));
    require(after.planHash == plan.planHash && after.status == (undo ? PlanStatus::RolledBack : PlanStatus::Completed) &&
        testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='recovery_choice_recorded'") == 1, "Mixed explicit recovery status/hash/audit differs");
    require(!service.execute(request), "Finished mixed recovery must not execute twice");
    require(take(journal.forStep(plan.steps[0].id)).size() == (undo ? 2 : 1) && take(journal.forStep(plan.steps[1].id)).empty(),
        "Recovery duplicated the completed first copy or created a rename transfer");
    if (!undo) {
        for (const auto& step : after.steps) require(step.status == StepStatus::Applied && !std::filesystem::exists(step.source) &&
            TransferValidation::sameContent(step.fingerprint, fingerprint(step.destination)), "Mixed resume lost approved contents");
        const auto attempts = take(journal.forStep(plan.steps[2].id));
        require(attempts.size() == 1 && attempts[0].phase == TransferPhase::Completed &&
            (lastAttempts.empty() || attempts[0].id == lastAttempts[0].id), "Mixed resume restarted rather than recovered the last attempt");
        saved(executor.rollback(plan.id));
    }
    for (size_t i = 0; i < plan.steps.size(); ++i) {
        const auto& step = plan.steps[i];
        require(!std::filesystem::exists(step.destination) && TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)), "Mixed recovery/undo lost an original");
        if (i == 1 || (undo && i == 2)) require(sameFingerprint(step.fingerprint, fingerprint(step.source)), "Rename or untouched source object changed");
    }
    if (undo && partial) {
        require(sameFingerprint(*partial, fingerprint(lastAttempts[0].staging)) && take(journal.load(lastAttempts[0].id)).phase == TransferPhase::StagingCreated,
            "Killed partial undo must not finish or delete staging");
        SqliteHistoryRepository reopened(database, migration);
        InspectInterruptedPlanUseCase reminders(reopened, journal, files, hasher, &copy);
        const auto retained = take(reminders.inspectAll());
        require(take(reopened.preservedTransfers(plan.id)).size() == 1 && retained.size() == 1 && retained[0].preserved == 1,
            "Killed partial undo lost its durable staging reminder");
    } else require(take(inspector.inspectAll()).empty(), "Finished non-preserved mixed task remains incomplete");
    std::cout << "RECOVERED " << choice.toStdString() << " " << QCoreApplication::applicationPid() << std::endl;
    return 0;
}

// 정리 중단은 새 프로세스에서 마무리하고 미지원 전송 경계는 무변경 차단하는 함수.
int edgeRecoveryChild(const QString& database, const QString& boundary) {
    require(QStringList{"cleanup-intent", "cleanup-removed", "cleanup-removed-occupant", "before-copy", "reverse-mid-copy"}.contains(boundary), "Unknown edge boundary");
    const QFileInfo db(database);
    require(db.isAbsolute() && db.isFile() && db.fileName() == "history.sqlite" && db.dir().dirName() == "app-data" &&
        QFileInfo(QFileInfo(db.dir().absolutePath()).dir().absolutePath()).dir().dirName().startsWith("NookBit-cross-target-"), "Existing owned edge DB required");
    OperationMutex lock;
    saved(lock.acquire());
    const auto migration = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
    SqliteHistoryRepository history(database, migration);
    SqliteTransferJournal journal(database, migration);
    const auto plan = ownedMixedPlan(history, database);
    QtPlanHasher hasher;
    WindowsStagedCopy copy;
    WindowsTransferSourceRemoval removal;
    WindowsTransferFileInspector files;
    InspectInterruptedPlanUseCase inspector(history, journal, files, hasher, &copy);
    const auto attempts = take(journal.forStep(plan.steps.back().id));
    const auto transferEvents = testSql(database, "SELECT COUNT(*) FROM transfer_events");
    const bool cleaning = boundary.startsWith("cleanup-");
    if (cleaning) {
        require(plan.status == PlanStatus::RolledBack && attempts.size() == 1 && attempts[0].direction == TransferDirection::Forward,
            "Killed cleanup must preserve completed undo and its partial attempt");
        const auto prior = take(history.preservedCleanup(plan.id, attempts[0].id));
        require(prior && !prior->removed && testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='preserved_cleanup_intent'") == 1 &&
            testSql(database, "SELECT COUNT(*) FROM events WHERE event_type='preserved_cleanup_removed'") == 0, "Killed cleanup must retain exactly its initial intent");
        const auto& stage = attempts[0].staging;
        if (boundary == "cleanup-intent") require(sameFingerprint(prior->stagingFingerprint, fingerprint(stage)), "Pre-delete crash lost the approved partial file");
        else require(!std::filesystem::exists(stage), "Post-delete crash still has staging");
        std::optional<FileFingerprint> occupant;
        if (boundary == "cleanup-removed-occupant") {
            createFile(stage, "owned post-crash staging occupant"); occupant = fingerprint(stage);
        }
        WindowsPreservedStagingCleanup worker;
        CleanupPreservedStagingUseCase cleanup(history, journal, files, worker, hasher);
        PreservedStagingCleanupService service(cleanup);
        const auto result = service.execute({*prior, true});
        if (occupant) {
            require(!result && sameFingerprint(*occupant, fingerprint(stage)) && !take(history.preservedCleanup(plan.id, attempts[0].id))->removed &&
                take(inspector.inspectAll()).size() == 1, "Post-crash cleanup must preserve a new occupant and retain its unresolved audit");
        } else {
            saved(result);
            require(!std::filesystem::exists(stage) && take(history.preservedCleanup(plan.id, attempts[0].id))->removed &&
                take(inspector.inspectAll()).empty() && !service.execute({*prior, true}), "Cleanup crash must finish exactly once and resolve its reminder");
        }
        for (const auto& step : plan.steps) require(TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)) &&
            !std::filesystem::exists(step.destination), "Cleanup crash recovery changed an original or final path");
    } else {
        const bool reverse = boundary == "reverse-mid-copy";
        require(plan.status == (reverse ? PlanStatus::RollingBack : PlanStatus::Executing) && attempts.size() == (reverse ? 2 : 1), "Killed edge plan state differs");
        const auto partial = std::find_if(attempts.begin(), attempts.end(), [&](const TransferAttempt& attempt) {
            return attempt.direction == (reverse ? TransferDirection::Rollback : TransferDirection::Forward);
        });
        require(partial != attempts.end() && partial->phase == (reverse ? TransferPhase::StagingCreated : TransferPhase::Prepared), "Killed transfer boundary differs");
        if (reverse) require(std::filesystem::file_size(partial->staging) == 1024 * 1024, "Reverse crash needs real 1MiB partial bytes");
        else require(!std::filesystem::exists(partial->staging), "Copy-before-start crash must not create staging");
        std::vector<std::pair<std::filesystem::path, std::optional<FileFingerprint>>> observations;
        for (const auto& step : plan.steps) {
            for (const auto& path : {step.source, step.destination})
                observations.emplace_back(path, std::filesystem::exists(path) ? std::optional<FileFingerprint>{fingerprint(path)} : std::nullopt);
            for (const auto& attempt : take(journal.forStep(step.id)))
                observations.emplace_back(attempt.staging, std::filesystem::exists(attempt.staging) ? std::optional<FileFingerprint>{fingerprint(attempt.staging)} : std::nullopt);
        }
        const auto events = testSql(database, "SELECT COUNT(*) FROM events");
        const auto report = take(inspector.execute(plan.id));
        require(report.planIntact && report.needsAttention == 1 && take(inspector.inspectAll()).size() == 1, "Unsupported edge must be visible for review");
        if (!reverse) require(report.steps.back().transfer && report.steps.back().transfer->finding == TransferRecoveryFinding::NoCopyObserved,
            "Prepared/no-staging boundary must be diagnosed without guessing a restart");
        BatchSettings settings;
        const auto appData = std::filesystem::path(db.dir().absolutePath().toStdWString());
        settings.value.homeRoot = appData.parent_path() / "organized";
        BatchRules rules;
        FilesystemScanner scanner;
        RuleEngine engine;
        Planner planner(engine, appData, CrossVolumeDrafts::Allow);
        VerifySavedPlanUseCase verifier(rules, scanner, planner, hasher, history, &copy);
        ExecuteTransferStepUseCase forward(copy, removal, journal, history, hasher);
        RollbackTransferUseCase undo(journal, history, copy, removal, files, hasher);
        SafeExecutor executor(settings, verifier, scanner, hasher, history, appData, &copy, &forward, &undo);
        WindowsTransferRecoveryGuard removalGuard;
        WindowsTransferCommitRecoveryGuard commitGuard;
        ResumeInterruptedTransferUseCase resolver(history, journal, copy, removal, files, removalGuard, commitGuard, hasher);
        RecoverInterruptedPlanUseCase recovery(inspector, history, settings, rules, hasher, executor, &resolver);
        InterruptedOperationRecoveryService service(recovery);
        saved(service.execute({plan.id, plan.planHash, RecoveryAction::Later}));
        for (const auto action : {RecoveryAction::Resume, RecoveryAction::Rollback})
            require(!service.execute({plan.id, plan.planHash, action}), "Unsupported copy-start/reverse-partial edge must not guess or delete");
        require(testSql(database, "SELECT COUNT(*) FROM events") == events &&
            PlanSignature::planContents(take(history.loadPlan(plan.id))) == PlanSignature::planContents(plan) &&
            take(history.loadPlan(plan.id)).status == plan.status, "Blocked edge changed movement/audit records");
        for (const auto& [path, before] : observations)
            require(before ? std::filesystem::exists(path) && sameFingerprint(*before, fingerprint(path)) : !std::filesystem::exists(path), "Blocked edge changed an owned file");
    }
    require(take(history.loadPlan(plan.id)).planHash == plan.planHash &&
        take(history.loadPlan(plan.id)).status == plan.status &&
        PlanSignature::planContents(take(history.loadPlan(plan.id))) == PlanSignature::planContents(plan) &&
        take(journal.forStep(plan.steps.back().id)).size() == attempts.size() && testSql(database, "SELECT COUNT(*) FROM transfer_events") == transferEvents,
        "Edge recovery changed movement/approval/transfer history");
    std::cout << "RECOVERED " << boundary.toStdString() << " " << QCoreApplication::applicationPid() << std::endl;
    return 0;
}

// 새 자식이 읽은 계획을 이번 실행이 만든 전용 시험 자료에만 연결하는 함수.
OperationPlan ownedPlan(SqliteHistoryRepository& history, const QString& database) {
    const QFileInfo db(database);
    require(db.isAbsolute() && db.isFile() && db.fileName() == "history.sqlite" && db.dir().dirName().startsWith("case-"), "Existing owned scenario DB required");
    const auto target = db.dir().absolutePath();
    const auto run = QFileInfo(target).dir().dirName();
    require(run.startsWith("NookBit-cross-target-"), "Owned test run root required before opening SQLite");
    const auto plan = take(history.loadPlan("plan"));
    require(plan.ruleSetVersion == "cross-volume-test" && plan.formatVersion == 2 &&
        plan.steps.size() == 1 && plan.steps[0].id == "step" && plan.steps[0].movement &&
        plan.steps[0].movement->strategy == MoveStrategy::VerifiedCopy, "Owned v2 test plan required");
    const auto& step = plan.steps[0];
    const auto sourceParent = QFileInfo(QString::fromStdWString(step.source.parent_path().wstring()));
    require(sourceParent.fileName() == db.dir().dirName() && sourceParent.dir().dirName().startsWith("NookBit-cross-source-") &&
        step.source.filename() == L"테스트 파일.bin" && step.destination.filename() == L"정리 파일.bin" &&
        QDir::cleanPath(QString::fromStdWString(step.destination.parent_path().parent_path().parent_path().wstring())) == QDir::cleanPath(target) &&
        step.fingerprint.size == 4 * 1024 * 1024 + 37 && step.fingerprint.sha256 ==
            std::optional<std::string>{"0f3d42aa1953c5eec4f644beb02e835ff574ad51ea26b8afbc0d0ea68bc3c852"}, "Unexpected test file paths or contents");
    QtPlanHasher hasher;
    require(plan.planHash == hasher.sha256(PlanSignature::approvalPayload(plan)), "Test plan signature changed");
    return plan;
}

// 저장된 v2 시험 계획을 새 프로세스에서 실행하거나 중단 후 복구하는 함수.
int child(const QString& database, const QString& stopAt, bool recovering) {
    require(QStringList{"mid-copy", "after-rename", "after-remove", "before-complete"}.contains(stopAt), "Unknown checkpoint");
    const auto migration = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
    OperationMutex lock;
    saved(lock.acquire());
    SqliteHistoryRepository history(database, migration);
    SqliteTransferJournal journal(database, migration);
    const auto plan = ownedPlan(history, database);
    FaultJournal faults(journal);
    WindowsStagedCopy copy;
    WindowsTransferSourceRemoval removal;
    WindowsTransferFileInspector files;
    QtPlanHasher hasher;
    if (!recovering) {
        require(take(journal.forStep("step")).empty(), "Crash worker requires a fresh owned scenario");
        faults.stopAt = stopAt;
        TransferAttempt attempt;
        attempt.id = "forward"; attempt.planId = plan.id; attempt.stepId = plan.steps[0].id;
        attempt.source = plan.steps[0].source; attempt.destination = plan.steps[0].destination;
        attempt.staging = attempt.destination.parent_path() / "stage.tmp";
        attempt.sourceFingerprint = plan.steps[0].fingerprint;
        CommitTransferCopyUseCase commit(copy, faults);
        // 실제 첫 블록 기록 이후에만 중단 지점에 도착하는 함수.
        take(commit.execute(attempt, [&] {
            if (stopAt == "mid-copy" && std::filesystem::exists(attempt.staging) &&
                std::filesystem::file_size(attempt.staging) >= 1024 * 1024) checkpoint(stopAt);
            return false;
        }, &*plan.steps[0].movement));
        FinalizeTransferUseCase finish(faults, removal, history, hasher);
        take(finish.execute("forward"));
        throw std::runtime_error("Worker completed without hitting requested checkpoint");
    }
    const auto pending = take(journal.load("forward"));
    WindowsTransferRecoveryGuard finalGuard;
    if (stopAt == "mid-copy") {
        require(pending.phase == TransferPhase::StagingCreated && std::filesystem::file_size(pending.staging) == 1024 * 1024 &&
            sameFingerprint(pending.sourceFingerprint, fingerprint(pending.source)), "Killed partial copy state differs");
        ResumePartialCopyUseCase resume(history, journal, copy, hasher);
        take(resume.execute("forward"));
        CommitResumedStagingUseCase commit(history, journal, copy, hasher);
        take(commit.execute("forward"));
        ResumeCommittedTransferUseCase finish(history, journal, removal, finalGuard, hasher);
        take(finish.execute("forward"));
    } else if (stopAt == "after-rename") {
        require(pending.phase == TransferPhase::CommitIntent && !pending.destinationFingerprint &&
            std::filesystem::exists(pending.source) && !std::filesystem::exists(pending.staging), "Killed rename state differs");
        WindowsTransferCommitRecoveryGuard guard;
        ReconcileTransferCommitUseCase recover(history, journal, guard, hasher);
        take(recover.execute("forward"));
        ResumeCommittedTransferUseCase finish(history, journal, removal, finalGuard, hasher);
        take(finish.execute("forward"));
    } else {
        require(pending.phase == (stopAt == "after-remove" ? TransferPhase::SourceRemoveIntent : TransferPhase::SourceRemoved) &&
            !std::filesystem::exists(pending.source) && !std::filesystem::exists(pending.staging), "Killed source removal state differs");
        ReconcileTransferRecoveryUseCase recover(history, journal, finalGuard, hasher);
        take(recover.execute("forward"));
    }
    const auto completed = take(journal.load("forward"));
    require(completed.phase == TransferPhase::Completed && take(history.loadPlan(plan.id)).status == PlanStatus::Completed &&
        !std::filesystem::exists(completed.source) && TransferValidation::sameContent(completed.sourceFingerprint, fingerprint(completed.destination)),
        "New process did not safely finish movement");
    RollbackTransferUseCase undo(journal, history, copy, removal, files, hasher);
    take(undo.execute("forward", "reverse", completed.source.parent_path() / "reverse-stage.tmp"));
    require(take(history.loadPlan(plan.id)).status == PlanStatus::RolledBack &&
        !std::filesystem::exists(completed.destination) && TransferValidation::sameContent(completed.sourceFingerprint, fingerprint(completed.source)),
        "Recovered move cannot be safely rolled back");
    std::cout << "RECOVERED " << stopAt.toStdString() << " " << QCoreApplication::applicationPid() << std::endl;
    return 0;
}

// 자신이 실행한 자식만 종료하도록 시험 프로세스의 경로와 인자를 설정하는 함수.
void configureChild(QProcess& process, const QString& mode, const QString& database, const QString& stopAt) {
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({mode, database, stopAt});
    process.setProcessChannelMode(QProcess::SeparateChannels);
}

// 체크포인트에 도착한 자기 자식만 강제 종료하고 새 자식의 복구 결과를 검사하는 함수.
QJsonObject crashScenario(const QString& sourceRun, const QString& targetRun, int index, const QString& stopAt, bool mixed = false, bool edge = false) {
    QString database;
    if (mixed) database = QDir(targetRun).filePath(QString("case-%1/app-data/history.sqlite").arg(index + (edge ? 52 : 32)));
    else {
        OperationMutex lock;
        saved(lock.acquire());
        Fixture fixture(sourceRun, targetRun, QString("case-%1").arg(index));
        database = fixture.database;
    } // 부모의 DB 연결·잠금은 자식 실행 전에 닫는다.
    QProcess worker;
    if (mixed) {
        worker.setProgram(QCoreApplication::applicationFilePath());
        worker.setArguments({edge ? "--edge-crash-worker" : "--mixed-crash-worker", sourceRun, targetRun, QString::number(index + (edge ? 52 : 32)), stopAt.section('|', 0, 0)});
        worker.setProcessChannelMode(QProcess::SeparateChannels);
    } else configureChild(worker, "--crash-worker", database, stopAt);
    worker.start();
    require(worker.waitForStarted(5000), "Cannot launch owned crash worker");
    const auto workerPid = worker.processId();
    QByteArray output;
    const auto marker = QString("CHECKPOINT %1 %2").arg(mixed ? stopAt.section('|', 0, 0) : stopAt).arg(workerPid).toUtf8();
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 15000 && worker.state() != QProcess::NotRunning && !output.contains(marker)) {
        worker.waitForReadyRead(500);
        output += worker.readAllStandardOutput();
    }
    const bool reached = output.contains(marker);
    // 이름으로 일괄 종료하지 않고 이 QProcess가 소유한 자식 핸들만 사용한다.
    if (worker.state() != QProcess::NotRunning) worker.kill();
    const bool stopped = worker.waitForFinished(5000) || worker.state() == QProcess::NotRunning;
    require(reached && stopped && worker.exitStatus() == QProcess::CrashExit,
        "Owned worker was not killed at checkpoint: " + worker.readAllStandardError().toStdString());
    QProcess recovery;
    configureChild(recovery, edge ? "--edge-crash-recover" : mixed ? "--mixed-crash-recover" : "--crash-recover", database, stopAt);
    recovery.start();
    require(recovery.waitForStarted(5000), "Cannot launch new recovery process");
    const auto recoveryPid = recovery.processId();
    const bool finished = recovery.waitForFinished(30000);
    if (!finished) { recovery.kill(); recovery.waitForFinished(5000); }
    const auto recoveryOutput = recovery.readAllStandardOutput();
    require(finished && recovery.exitStatus() == QProcess::NormalExit && recovery.exitCode() == 0 &&
        recoveryOutput.contains(QString("RECOVERED %1 %2").arg(stopAt).arg(recoveryPid).toUtf8()),
        "New process recovery failed: " + recovery.readAllStandardError().toStdString());
    const auto migration = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
    OperationMutex lock;
    saved(lock.acquire());
    SqliteHistoryRepository history(database, migration);
    SqliteTransferJournal journal(database, migration);
    const auto plan = mixed ? ownedMixedPlan(history, database) : ownedPlan(history, database);
    if (edge) {
        const bool cleaning = stopAt.startsWith("cleanup-");
        require(plan.status == (cleaning ? PlanStatus::RolledBack : stopAt == "before-copy" ? PlanStatus::Executing : PlanStatus::RollingBack), "Parent edge state differs");
        for (size_t i = 0; i < plan.steps.size(); ++i) {
            const auto& step = plan.steps[i];
            const bool atSource = cleaning || (stopAt == "before-copy" && i == 2);
            require(TransferValidation::sameContent(step.fingerprint, fingerprint(atSource ? step.source : step.destination)) &&
                !std::filesystem::exists(atSource ? step.destination : step.source), "Parent edge file conservation failed");
        }
        if (cleaning) {
            const auto retained = take(history.preservedTransfers(plan.id));
            require(retained.size() == 1, "Parent cleanup lost preservation history");
            const auto audit = take(history.preservedCleanup(plan.id, retained[0].attemptId));
            require(audit && audit->removed == (stopAt != "cleanup-removed-occupant") &&
                std::filesystem::exists(retained[0].staging) == (stopAt == "cleanup-removed-occupant"), "Parent cleanup audit/location differs");
        }
    } else if (mixed) {
        require(plan.status == PlanStatus::RolledBack, "Parent found unfinished rollback");
        for (const auto& step : plan.steps) require(TransferValidation::sameContent(step.fingerprint, fingerprint(step.source)) &&
            !std::filesystem::exists(step.destination), "Parent independent mixed original check failed");
        const auto retained = take(history.preservedTransfers(plan.id));
        require(retained.size() == (stopAt == "mid-copy|undo" ? 1 : 0), "Parent preserved registration differs");
        if (!retained.empty()) require(std::filesystem::file_size(retained[0].staging) == 1024 * 1024, "Parent partial staging missing");
    } else require(plan.status == PlanStatus::RolledBack && plan.steps[0].status == StepStatus::RolledBack &&
        take(journal.load("forward")).phase == TransferPhase::Completed && take(journal.load("reverse")).phase == TransferPhase::Completed &&
        TransferValidation::sameContent(plan.steps[0].fingerprint, fingerprint(plan.steps[0].source)) && !std::filesystem::exists(plan.steps[0].destination),
        "Parent independent check of recovered DB and restored file failed");
    return {{"case", stopAt}, {"passed", true}, {"workerPid", workerPid}, {"recoveryPid", recoveryPid},
        {"outcome", edge ? stopAt == "before-copy" || stopAt == "reverse-mid-copy" ? "unsupported-preserved-and-blocked" :
            stopAt == "cleanup-removed-occupant" ? "new-occupant-preserved-and-blocked" : "cleanup-completed" : "recovered-and-rolled-back"},
        {"forcedTermination", true}, {"workerOutput", QString::fromUtf8(output)}, {"recoveryOutput", QString::fromUtf8(recoveryOutput)}};
}
}

// 명시한 두 기존 부모가 서로 다른 볼륨일 때만 새 전용 폴더에서 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 6 && (args[1] == "--mixed-crash-worker" || args[1] == "--edge-crash-worker")) {
        try {
            require(QDir(args[2]).dirName().startsWith("NookBit-cross-source-") && QDir(args[3]).dirName().startsWith("NookBit-cross-target-") &&
                QFileInfo(args[2]).isDir() && QFileInfo(args[3]).isDir(), "Owned mixed run directories required");
            bool valid = false;
            const int index = args[4].toInt(&valid);
            const bool edge = args[1] == "--edge-crash-worker";
            require(valid && (edge ? index >= 52 && index < 57 && QStringList{"cleanup-intent", "cleanup-removed", "cleanup-removed-occupant", "before-copy", "reverse-mid-copy"}.contains(args[5]) :
                index >= 32 && index < 39 && QStringList{"before-last", "mid-copy", "after-rename", "after-remove", "before-complete"}.contains(args[5])),
                "Owned mixed worker arguments required");
            OperationMutex lock;
            saved(lock.acquire());
            batchScenario(args[2], args[3], index, args[5]);
            return 2;
        } catch (const std::exception& error) { std::cerr << "Mixed worker failed: " << error.what() << '\n'; return 2; }
    }
    if (args.size() == 4 && args[1] == "--edge-crash-recover") {
        try { return edgeRecoveryChild(args[2], args[3]); }
        catch (const std::exception& error) { std::cerr << "Edge recovery failed: " << error.what() << '\n'; return 2; }
    }
    if (args.size() == 4 && args[1] == "--mixed-crash-recover") {
        try { return mixedRecoveryChild(args[2], args[3]); }
        catch (const std::exception& error) { std::cerr << "Mixed recovery failed: " << error.what() << '\n'; return 2; }
    }
    if (args.size() == 4 && (args[1] == "--crash-worker" || args[1] == "--crash-recover")) {
        try { return child(args[2], args[3], args[1] == "--crash-recover"); }
        catch (const std::exception& error) { std::cerr << "Child failed: " << error.what() << '\n'; return 2; }
    }
    if (args.size() != 6 || (args[1] != "--run" && args[1] != "--zone-recovery-run" && args[1] != "--crash-run" && args[1] != "--mixed-crash-run" && args[1] != "--edge-crash-run" && args[1] != "--batch-run" && args[1] != "--recovery-run" && args[1] != "--transfer-recovery-run" && args[1] != "--preserved-cleanup-run" && args[1] != "--preserved-undo-run") || args[2] != "--source-parent" || args[4] != "--destination-parent") {
        std::cout << "Opt-in only: --run, --zone-recovery-run, --crash-run, --mixed-crash-run, --edge-crash-run, --batch-run, --recovery-run, --transfer-recovery-run, --preserved-undo-run or --preserved-cleanup-run --source-parent <existing-directory> --destination-parent <existing-directory>\n";
        return args.size() == 1 ? 0 : 2;
    }
    try {
        for (const auto& parent : {args[3], args[5]})
            require(QFileInfo(parent).isAbsolute() && QFileInfo(parent).isDir(), "Absolute existing parent directories required");
        WindowsStagedCopy inspector;
        const auto sourceVolume = take(inspector.inspectVolume(std::filesystem::path(args[3].toStdWString())));
        const auto targetVolume = take(inspector.inspectVolume(std::filesystem::path(args[5].toStdWString())));
        require(sourceVolume.localNtfs && targetVolume.localNtfs && sourceVolume.name != targetVolume.name,
            "Two genuinely distinct fixed NTFS volumes required before creating any test folders");
        const bool edgeCrash = args[1] == "--edge-crash-run";
        const bool mixedCrash = args[1] == "--mixed-crash-run" || edgeCrash;
        const bool crashing = args[1] == "--crash-run" || mixedCrash;
        const bool batching = args[1] == "--batch-run";
        const bool recovering = args[1] == "--recovery-run";
        const bool resolving = args[1] == "--transfer-recovery-run";
        const bool preserving = args[1] == "--preserved-undo-run";
        const bool cleaning = args[1] == "--preserved-cleanup-run";
        const bool zoneRecovery = args[1] == "--zone-recovery-run";
        // 일반 시험은 전체 잠금, 강제 종료 시험은 각 자식이 독립적으로 잠금을 갖는다.
        std::unique_ptr<OperationMutex> lock;
        if (!crashing) { lock = std::make_unique<OperationMutex>(); saved(lock->acquire()); }
        QTemporaryDir source(QDir(args[3]).filePath("NookBit-cross-source-XXXXXX"));
        QTemporaryDir target(QDir(args[5]).filePath("NookBit-cross-target-XXXXXX"));
        source.setAutoRemove(false); target.setAutoRemove(false);
        require(source.isValid() && target.isValid(), "Cannot create unique owned test roots");
        std::cout << "Source fixtures: " << source.path().toStdString() << "\nTarget fixtures/report: " << target.path().toStdString() << '\n';
        QJsonArray results;
        int failures = 0;
        const QStringList names = zoneRecovery ? QStringList{"zone-partial-missing-marker", "zone-partial-matching-marker", "zone-partial-mismatch-blocked", "zone-verified-staging", "zone-unrecorded-commit", "zone-committed-source-retained", "zone-source-removed-blocked", "zone-completion-record-failed-blocked", "zone-final-mismatch-blocked"} :
            edgeCrash ? QStringList{"cleanup-intent", "cleanup-removed", "cleanup-removed-occupant", "before-copy", "reverse-mid-copy"} :
            cleaning ? QStringList{"explicit-partial-cleanup", "stale-plan-approval", "changed-original-blocked", "replaced-stage-blocked", "changed-prefix-blocked", "intent-audit-failure", "removed-audit-only-retry", "later-occupant-blocked", "stale-stage-approval", "readonly-stage-blocked", "final-occupant-blocked", "unrecorded-absence-blocked", "corrupt-cleanup-audit-visible"} :
            mixedCrash ? QStringList{"before-last|resume", "before-last|undo", "mid-copy|resume", "mid-copy|undo", "after-rename|resume", "after-remove|resume", "before-complete|resume"} :
            crashing ? QStringList{"mid-copy", "after-rename", "after-remove", "before-complete"} :
            preserving ? QStringList{"undo-prefix-preserve-partial", "preserved-undo-audit-failure", "changed-partial-source-blocked", "replaced-staging-blocked", "no-moves-partial-preserved", "late-undo-collision-retains-registration"} :
            resolving ? QStringList{"partial-then-whole-resume", "verified-stage-then-whole-resume", "commit-record-then-whole-resume", "committed-source-retained-resume", "removed-source-record-resume", "completion-record-resume", "last-partial-completes-whole", "partial-collision-preserved", "partial-audit-failure-preserved", "partial-undo-preserves-staging", "corrupt-prefix-stops-whole"} :
            recovering ? QStringList{"explicit-resume", "explicit-undo", "later-preserves", "changed-source-blocked", "changed-settings-blocked", "audit-atomic-failure", "stale-choice-blocked", "missing-service-blocked", "undo-record-completion", "resume-final-copy"} :
            batching ? QStringList{"copy-then-renames-roundtrip", "rename-copy-rename-roundtrip", "mixed-late-collision-compensation", "mixed-undo-collision-preflight", "copy-rename-copy-roundtrip"} :
            QStringList{"roundtrip", "forward-collision", "partial-resume-roundtrip", "commit-record-recovery-roundtrip", "removal-record-recovery-roundtrip", "reverse-collision"};
        for (int index = 0; index < names.size(); ++index) {
            try {
                if (crashing) results.append(crashScenario(source.path(), target.path(), index, names[index], mixedCrash, edgeCrash));
                else {
                    if (zoneRecovery) zoneRecoveryScenario(source.path(), target.path(), index);
                    else if (batching || recovering || resolving || preserving || cleaning) batchScenario(source.path(), target.path(), cleaning ? index + 39 : preserving ? index + 26 : resolving ? index + 15 : recovering ? index + 5 : index);
                    else scenario(source.path(), target.path(), index);
                    results.append(QJsonObject{{"case", names[index]}, {"passed", true}});
                }
                std::cout << "PASS " << names[index].toStdString() << '\n';
            } catch (const std::exception& error) {
                ++failures;
                results.append(QJsonObject{{"case", names[index]}, {"passed", false}, {"error", QString::fromUtf8(error.what())}});
                std::cerr << "FAIL " << names[index].toStdString() << ": " << error.what() << '\n';
            }
        }
        QSaveFile report(QDir(target.path()).filePath(zoneRecovery ? "zone-recovery-report.json" : edgeCrash ? "edge-crash-report.json" : cleaning ? "preserved-cleanup-report.json" : mixedCrash ? "mixed-crash-report.json" : crashing ? "crash-report.json" : preserving ? "preserved-undo-report.json" : resolving ? "transfer-recovery-report.json" : recovering ? "recovery-report.json" : batching ? "batch-report.json" : "report.json"));
        require(report.open(QIODevice::WriteOnly), "Cannot save test report");
        const auto bytes = QJsonDocument(QJsonObject{{"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"sourceRoot", source.path()}, {"targetRoot", target.path()}, {"sourceVolume", QString::fromStdWString(sourceVolume.name)},
            {"destinationVolume", QString::fromStdWString(targetVolume.name)}, {"mode", edgeCrash ? "cleanup-and-unsupported-edge-termination" : cleaning ? "preserved-partial-cleanup" : mixedCrash ? "mixed-forced-process-termination" : crashing ? "forced-process-termination" : preserving ? "partial-preserving-undo" : resolving ? "transfer-to-batch-recovery" : recovering ? "explicit-batch-recovery" : batching ? "native-mixed-batch" : "record-fault-injection"},
            {"downloadMarkerRecovery", zoneRecovery}, {"passed", failures == 0}, {"cases", results}}).toJson();
        require(report.write(bytes) == bytes.size() && report.commit(), "Test report commit failed");
        std::cout << "Fixtures retained; no recursive cleanup performed.\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Test stopped: " << error.what() << '\n';
        return 2;
    }
}
