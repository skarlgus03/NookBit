#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include <nookbit/application/rollback_transfer_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
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
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <memory>

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
    // 실제 SQLite 저널을 빌려 받는 함수.
    explicit FaultJournal(ITransferJournal& inner) : inner_(inner) {}
    // 새 시도 기록을 전달하는 함수.
    Result<void> begin(const TransferAttempt& attempt) override { return inner_.begin(attempt); }
    // 실제 파일 작업 뒤 지정한 기록 경계에서 한 번만 실패하는 함수.
    Result<void> advance(const std::string& id, const TransferUpdate& update) override {
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
        if (stopAt == "before-complete") checkpoint(stopAt);
        return inner_.completeForward(id);
    }
    // 원자적 역방향 완료를 전달하는 함수.
    Result<void> completeRollback(const std::string& id) override { return inner_.completeRollback(id); }
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
    Fixture(const QString& sourceRun, const QString& targetRun, const QString& name)
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
QJsonObject crashScenario(const QString& sourceRun, const QString& targetRun, int index, const QString& stopAt) {
    QString database;
    {
        OperationMutex lock;
        saved(lock.acquire());
        Fixture fixture(sourceRun, targetRun, QString("case-%1").arg(index));
        database = fixture.database;
    } // 부모의 DB 연결·잠금은 자식 실행 전에 닫는다.
    QProcess worker;
    configureChild(worker, "--crash-worker", database, stopAt);
    worker.start();
    require(worker.waitForStarted(5000), "Cannot launch owned crash worker");
    const auto workerPid = worker.processId();
    QByteArray output;
    const auto marker = QString("CHECKPOINT %1 %2").arg(stopAt).arg(workerPid).toUtf8();
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
    configureChild(recovery, "--crash-recover", database, stopAt);
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
    const auto plan = ownedPlan(history, database);
    require(plan.status == PlanStatus::RolledBack && plan.steps[0].status == StepStatus::RolledBack &&
        take(journal.load("forward")).phase == TransferPhase::Completed && take(journal.load("reverse")).phase == TransferPhase::Completed &&
        TransferValidation::sameContent(plan.steps[0].fingerprint, fingerprint(plan.steps[0].source)) && !std::filesystem::exists(plan.steps[0].destination),
        "Parent independent check of recovered DB and restored file failed");
    return {{"case", stopAt}, {"passed", true}, {"workerPid", workerPid}, {"recoveryPid", recoveryPid},
        {"forcedTermination", true}, {"workerOutput", QString::fromUtf8(output)}, {"recoveryOutput", QString::fromUtf8(recoveryOutput)}};
}
}

// 명시한 두 기존 부모가 서로 다른 볼륨일 때만 새 전용 폴더에서 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && (args[1] == "--crash-worker" || args[1] == "--crash-recover")) {
        try { return child(args[2], args[3], args[1] == "--crash-recover"); }
        catch (const std::exception& error) { std::cerr << "Child failed: " << error.what() << '\n'; return 2; }
    }
    if (args.size() != 6 || (args[1] != "--run" && args[1] != "--crash-run") || args[2] != "--source-parent" || args[4] != "--destination-parent") {
        std::cout << "Opt-in only: --run or --crash-run --source-parent <existing-directory> --destination-parent <existing-directory>\n";
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
        const bool crashing = args[1] == "--crash-run";
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
        const QStringList names = crashing ? QStringList{"mid-copy", "after-rename", "after-remove", "before-complete"} :
            QStringList{"roundtrip", "forward-collision", "partial-resume-roundtrip", "commit-record-recovery-roundtrip", "removal-record-recovery-roundtrip", "reverse-collision"};
        for (int index = 0; index < names.size(); ++index) {
            try {
                if (crashing) results.append(crashScenario(source.path(), target.path(), index, names[index]));
                else { scenario(source.path(), target.path(), index); results.append(QJsonObject{{"case", names[index]}, {"passed", true}}); }
                std::cout << "PASS " << names[index].toStdString() << '\n';
            } catch (const std::exception& error) {
                ++failures;
                results.append(QJsonObject{{"case", names[index]}, {"passed", false}, {"error", QString::fromUtf8(error.what())}});
                std::cerr << "FAIL " << names[index].toStdString() << ": " << error.what() << '\n';
            }
        }
        QSaveFile report(QDir(target.path()).filePath(crashing ? "crash-report.json" : "report.json"));
        require(report.open(QIODevice::WriteOnly), "Cannot save test report");
        const auto bytes = QJsonDocument(QJsonObject{{"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"sourceRoot", source.path()}, {"targetRoot", target.path()}, {"sourceVolume", QString::fromStdWString(sourceVolume.name)},
            {"destinationVolume", QString::fromStdWString(targetVolume.name)}, {"mode", crashing ? "forced-process-termination" : "record-fault-injection"},
            {"passed", failures == 0}, {"cases", results}}).toJson();
        require(report.write(bytes) == bytes.size() && report.commit(), "Test report commit failed");
        std::cout << "Fixtures retained; no recursive cleanup performed.\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Test stopped: " << error.what() << '\n';
        return 2;
    }
}
