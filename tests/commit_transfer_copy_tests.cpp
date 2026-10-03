#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/inspect_transfer_recovery_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/rollback_transfer_use_case.hpp>
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QUuid>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
using namespace nookbit;
int failures{};
// 기대와 다른 결과를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 테스트 전용 파일을 작성하는 함수.
void write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary);
    file << contents;
    expect(file.good(), "fixture write");
}
// 기존 스캐너와 Qt 해시로 디스크 파일 지문을 읽는 함수.
FileFingerprint fingerprint(const std::filesystem::path& path) {
    FilesystemScanner scanner;
    QtPlanHasher hasher;
    const auto result = scanFingerprint(scanner, path, &hasher);
    expect(bool(result), "fixture fingerprint");
    return result ? result.value() : FileFingerprint{};
}
// 기존 SQLite 계약 앞에서 특정 기록 경계의 실패를 주입하는 어댑터.
class FaultJournal final : public ITransferJournal {
public:
    ITransferJournal& inner;
    std::optional<TransferPhase> failAt;
    std::function<void(TransferPhase)> beforeRecord;
    bool persistent{false};
    TransferPhase last{TransferPhase::Prepared};
    // 실제 저널을 빌려 받는 함수.
    explicit FaultJournal(ITransferJournal& journal) : inner(journal) {}
    // 새 시도를 실제 DB에 기록하는 함수.
    Result<void> begin(const TransferAttempt& attempt) override { return inner.begin(attempt); }
    // 지정한 경계에서만 실패하거나 실제 DB에 단계 기록을 전달하는 함수.
    Result<void> advance(const std::string& id, const TransferUpdate& update) override {
        if (beforeRecord) beforeRecord(update.nextPhase);
        if (persistent || failAt == update.nextPhase) {
            failAt.reset();
            return Result<void>::failure({"test.journal", "Injected record failure"});
        }
        const auto result = inner.advance(id, update);
        if (result) last = update.nextPhase;
        return result;
    }
    // 제거 뒤 완료 트랜잭션 실패를 주입하거나 실제 저널로 전달하는 함수.
    Result<void> completeForward(const std::string& id) override {
        if (beforeRecord) beforeRecord(TransferPhase::Completed);
        if (persistent || failAt == TransferPhase::Completed) {
            failAt.reset();
            return Result<void>::failure({"test.completion", "Injected completion failure"});
        }
        const auto result = inner.completeForward(id);
        if (result) last = TransferPhase::Completed;
        return result;
    }
    // 역전송의 완료 저장 실패를 주입하거나 실제 저널로 전달하는 함수.
    Result<void> completeRollback(const std::string& id) override {
        if (beforeRecord) beforeRecord(TransferPhase::Completed);
        if (persistent || failAt == TransferPhase::Completed) {
            failAt.reset();
            return Result<void>::failure({"test.completion", "Injected rollback completion failure"});
        }
        const auto result = inner.completeRollback(id);
        if (result) last = TransferPhase::Completed;
        return result;
    }
    // 재시작 검사를 위한 시도 복원을 전달하는 함수.
    Result<TransferAttempt> load(const std::string& id) const override { return inner.load(id); }
    // 같은 단계의 중복 시도 조회를 전달하는 함수.
    Result<std::vector<TransferAttempt>> forStep(const StepId& id) const override { return inner.forStep(id); }
};

// 실제 이름 변경 직후 API 검증 실패를 주입하는 복사 어댑터.
class PostRenameFaultCopy final : public IStagedCopy {
    IStagedCopy& inner_;
    class Session final : public IStagedCopySession {
        std::unique_ptr<IStagedCopySession> inner_;
    public:
        // 실제 세션 소유권을 넘겨 받는 함수.
        explicit Session(std::unique_ptr<IStagedCopySession> inner) : inner_(std::move(inner)) {}
        // 실제 임시 파일 생성 증거를 전달하는 함수.
        const FileFingerprint& createdFingerprint() const override { return inner_->createdFingerprint(); }
        // 실제 복사·검증을 전달하는 함수.
        Result<FileFingerprint> copyAndVerify(const std::function<bool()>& cancelled) override {
            return inner_->copyAndVerify(cancelled);
        }
        // 실제 확정 후 실패를 반환해 확정 여부가 불확실한 경계를 재현하는 함수.
        Result<FileFingerprint> commitNoReplace(const std::filesystem::path& destination) override {
            const auto result = inner_->commitNoReplace(destination);
            if (!result) return result;
            return Result<FileFingerprint>::failure({"test.after_rename", "Injected failure after rename"});
        }
    };
public:
    // 실제 복사 일꾼을 빌려 받는 함수.
    explicit PostRenameFaultCopy(IStagedCopy& inner) : inner_(inner) {}
    // 실제 볼륨 조회를 전달하는 함수.
    Result<TransferVolume> inspectVolume(const std::filesystem::path& path) override { return inner_.inspectVolume(path); }
    // 실제 세션에 확정 후 실패 어댑터만 덧씌우는 함수.
    Result<std::unique_ptr<IStagedCopySession>> open(const std::filesystem::path& source,
        const FileFingerprint& expected, const std::filesystem::path& staging) override {
        auto result = inner_.open(source, expected, staging);
        if (!result) return Result<std::unique_ptr<IStagedCopySession>>::failure(result.error());
        return Result<std::unique_ptr<IStagedCopySession>>::success(std::make_unique<Session>(std::move(result.value())));
    }
};

// 임시 파일·승인된 기존 계획·실제 SQLite를 함께 준비하는 테스트 환경.
struct Fixture {
    QTemporaryDir directory{QCoreApplication::applicationDirPath() + "/commit-copy-XXXXXX"};
    QString migration{QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql"};
    QString database{directory.filePath("history.sqlite")};
    SqliteHistoryRepository history{database, migration};
    SqliteTransferJournal journal{database, migration};
    FaultJournal faults{journal};
    WindowsStagedCopy worker;
    CommitTransferCopyUseCase useCase{worker, faults};
    TransferAttempt attempt;
    bool ready{};
    // 원본 파일과 서명·승인·실행 의도를 실제 DB에 저장하는 함수.
    explicit Fixture(bool multiple = false) {
        expect(directory.isValid(), "temporary fixture");
        const std::filesystem::path root(directory.path().toStdWString());
        write(root / "source", "original contents for transfer copy");
        attempt.id = "attempt";
        attempt.planId = "plan";
        attempt.stepId = "step";
        attempt.source = root / "source";
        attempt.staging = root / "stage";
        attempt.destination = root / "destination";
        attempt.sourceFingerprint = fingerprint(attempt.source);
        OperationPlan plan;
        plan.id = attempt.planId;
        plan.ruleSetVersion = "v3";
        plan.settingsRevision = std::string(64, 'b');
        plan.rulesRevision = std::string(64, 'c');
        plan.status = PlanStatus::Planned;
        MoveStep step;
        step.id = attempt.stepId;
        step.source = attempt.source;
        step.destination = attempt.destination;
        step.fingerprint = attempt.sourceFingerprint;
        step.matchedRuleId = "rule";
        plan.steps.push_back(step);
        if (multiple) {
            auto pending = step;
            pending.id = "pending-step";
            pending.source = root / "pending-source";
            pending.destination = root / "pending-destination";
            write(pending.source, "pending contents");
            pending.fingerprint = fingerprint(pending.source);
            plan.steps.push_back(pending);
        }
        QtPlanHasher hasher;
        plan.planHash = hasher.sha256(PlanSignature::approvalPayload(plan));
        ready = history.savePlan(plan).hasValue() && history.saveApproval(
            {plan.id, plan.planHash, std::chrono::system_clock::now()}).hasValue() &&
            history.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing}).hasValue() &&
            history.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::IntentRecorded}).hasValue();
        expect(ready, "signed plan fixture");
    }
    // 원본 보존과 재시작 후 단계 복원을 검사하는 함수.
    void check(TransferPhase phase) {
        expect(sameFingerprint(attempt.sourceFingerprint, fingerprint(attempt.source)), "source always retained");
        SqliteTransferJournal reopened(database, migration);
        const auto result = reopened.load(attempt.id);
        expect(result && result.value().phase == phase, "phase restored from SQLite events");
        const auto plan = history.loadPlan(attempt.planId);
        expect(plan && plan.value().status == PlanStatus::Executing &&
            plan.value().steps.front().status == StepStatus::IntentRecorded, "copy commit is not movement completion");
    }
};

// 실제 복사본 확정·중복 거부·취소를 검사하는 함수.
void testSuccessAndCancel() {
    {
        Fixture fixture;
        if (!fixture.ready) return;
        const auto result = fixture.useCase.execute(fixture.attempt);
        if (!result) std::cerr << result.error().code << ": " << result.error().message << '\n';
        expect(result && result.value().phase == TransferPhase::DestinationCommitted, "commit succeeds without completing transfer");
        expect(!std::filesystem::exists(fixture.attempt.staging), "staging renamed not duplicated");
        expect(std::filesystem::exists(fixture.attempt.destination), "destination exists");
        if (result) expect(sameFingerprint(*result.value().destinationFingerprint, fingerprint(fixture.attempt.destination)), "final evidence matches disk");
        fixture.check(TransferPhase::DestinationCommitted);
        expect(!fixture.useCase.execute(fixture.attempt), "same attempt cannot execute twice");
        auto duplicate = fixture.attempt;
        duplicate.id = "second";
        duplicate.staging = duplicate.staging.parent_path() / "second-stage";
        expect(!fixture.useCase.execute(duplicate), "committed unresolved step blocks new attempts");
        expect(!std::filesystem::exists(duplicate.staging), "duplicate creates no files");
    }
    for (const auto cancelAt : {TransferPhase::Prepared, TransferPhase::StagingCreated, TransferPhase::CopyVerified}) {
        Fixture fixture;
        if (!fixture.ready) return;
        const auto result = fixture.useCase.execute(fixture.attempt, [&] { return fixture.faults.last == cancelAt; });
        expect(!result && result.error().code == "transfer.cancelled", "pre-commit cancellation reported");
        fixture.check(TransferPhase::Cancelled);
        expect(!std::filesystem::exists(fixture.attempt.destination), "cancel does not commit");
        if (cancelAt == TransferPhase::Prepared) expect(!std::filesystem::exists(fixture.attempt.staging), "early cancel creates no files");
    }
}

// 각 기록 경계의 실패로 다음 파일 작업이 실행되지 않는지 검사하는 함수.
void testJournalFailures() {
    for (const auto phase : {TransferPhase::CopyIntent, TransferPhase::StagingCreated,
            TransferPhase::CopyVerified, TransferPhase::CommitIntent, TransferPhase::DestinationCommitted}) {
        Fixture fixture;
        if (!fixture.ready) return;
        fixture.faults.failAt = phase;
        const auto result = fixture.useCase.execute(fixture.attempt);
        expect(!result, "record failure never succeeds");
        fixture.check(TransferPhase::NeedsRecovery);
        expect(std::filesystem::exists(fixture.attempt.destination) == (phase == TransferPhase::DestinationCommitted), "rename only after durable intent");
        if (phase == TransferPhase::CopyIntent) expect(!std::filesystem::exists(fixture.attempt.staging), "copy intent failure creates nothing");
        if (phase == TransferPhase::StagingCreated) expect(std::filesystem::file_size(fixture.attempt.staging) == 0, "unrecorded staging is not copied");
        expect(!fixture.useCase.execute(fixture.attempt), "recovery state blocks retry");
    }
    Fixture fixture;
    fixture.faults.persistent = true;
    const auto result = fixture.useCase.execute(fixture.attempt);
    expect(!result && result.error().code == "transfer.record_failed", "recovery record failure exposed");
    fixture.check(TransferPhase::Prepared);
    expect(!std::filesystem::exists(fixture.attempt.staging), "persistent DB failure touches no files");
}

// 최종 이름 충돌·원본 불일치·잘못된 요청을 검사하는 함수.
void testRejectedFiles() {
    {
        Fixture fixture;
        PostRenameFaultCopy worker(fixture.worker);
        CommitTransferCopyUseCase useCase(worker, fixture.faults);
        expect(!useCase.execute(fixture.attempt), "post-rename API failure never reports success");
        fixture.check(TransferPhase::NeedsRecovery);
        expect(std::filesystem::exists(fixture.attempt.destination) &&
            !std::filesystem::exists(fixture.attempt.staging), "renamed file retained for recovery after API error");
    }
    {
        Fixture fixture;
        fixture.faults.beforeRecord = [&](TransferPhase phase) {
            if (phase == TransferPhase::DestinationCommitted) fixture.faults.persistent = true;
        };
        const auto result = fixture.useCase.execute(fixture.attempt);
        expect(!result && result.error().code == "transfer.record_failed", "persistent post-commit DB failure reported");
        fixture.check(TransferPhase::CommitIntent);
        expect(std::filesystem::exists(fixture.attempt.destination), "committed file preserved when all result writes fail");
        expect(!fixture.useCase.execute(fixture.attempt), "unresolved commit intent blocks retry");
    }
    {
        Fixture fixture;
        fixture.faults.beforeRecord = [&](TransferPhase phase) {
            if (phase == TransferPhase::CommitIntent) write(fixture.attempt.destination, "keep collision file");
        };
        const auto result = fixture.useCase.execute(fixture.attempt);
        expect(!result, "racing destination collision rejected");
        fixture.check(TransferPhase::NeedsRecovery);
        QtPlanHasher hasher;
        expect(fingerprint(fixture.attempt.destination).sha256 == hasher.sha256("keep collision file"), "existing destination not overwritten");
        expect(std::filesystem::exists(fixture.attempt.staging), "verified staging retained on collision");
    }
    {
        Fixture fixture;
        auto changed = fixture.attempt;
        changed.sourceFingerprint.sha256 = std::string(64, '0');
        // SQLite가 승인 원본과 다른 지문을 begin 단계에서 거부해야 한다.
        expect(!fixture.useCase.execute(changed), "unapproved source evidence rejected by journal");
        expect(!std::filesystem::exists(changed.staging), "bad approval creates no staging");
    }
    {
        Fixture fixture;
        write(fixture.attempt.staging, "preexisting staging");
        const auto existing = fingerprint(fixture.attempt.staging);
        expect(!fixture.useCase.execute(fixture.attempt), "existing staging rejected");
        fixture.check(TransferPhase::NeedsRecovery);
        expect(sameFingerprint(existing, fingerprint(fixture.attempt.staging)), "foreign staging retained");
    }
}

// 복구 읽기 전후 비교를 위해 실제 DB의 전송 이벤트 개수를 읽는 함수.
int eventCount(const QString& path) {
    const auto name = QUuid::createUuid().toString();
    int count = -1;
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", name);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec("SELECT COUNT(*) FROM transfer_events") && query.next()) count = query.value(0).toInt();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(name);
    expect(count >= 0, "read event count");
    return count;
}

// 접근 불가·다른 볼륨을 실제 파일 변경 없이 주입하는 읽기 전용 어댑터.
class FaultInspector final : public ITransferFileInspector {
public:
    WindowsTransferFileInspector inner;
    std::filesystem::path overridePath;
    TransferFileObservation replacement;
    int reads{};
    // 특정 경로만 관찰값을 대체하고 나머지는 실제 파일을 검사하는 함수.
    TransferFileObservation inspect(const std::filesystem::path& path) override {
        ++reads;
        return path == overridePath ? replacement : inner.inspect(path);
    }
};

// 계획 서명 불일치 시 파일을 읽지 않는지 검사하기 위한 해시 포트.
class WrongHasher final : public IPlanHasher {
public:
    // 항상 잘못된 계획 해시를 반환하는 함수.
    std::string sha256(std::string_view) const override { return std::string(64, '0'); }
    // 이 테스트에서는 호출되면 안 되는 파일 해시 함수.
    Result<std::string> sha256File(const std::filesystem::path&) const override {
        return Result<std::string>::failure({"test.unused", "Unexpected file hashing"});
    }
};

// 복구 결과·이벤트 무변경·원본 보존을 함께 검사하는 함수.
void inspectRecovery(Fixture& fixture, TransferRecoveryFinding expected) {
    WindowsTransferFileInspector files;
    QtPlanHasher hasher;
    InspectTransferRecoveryUseCase inspect(fixture.history, fixture.journal, files, hasher);
    const auto before = eventCount(fixture.database);
    const auto result = inspect.execute(fixture.attempt.id);
    if (result && result.value().finding != expected) std::cerr << result.value().reason << '\n';
    expect(result && result.value().finding == expected, "three-location recovery classification");
    expect(before == eventCount(fixture.database), "recovery inspection changes no journal events");
}

// 세 위치의 실제 파일 상태와 미해결 전송 기록의 읽기 전용 복구를 검사하는 함수.
void testReadOnlyTransferRecovery() {
    {
        Fixture fixture;
        expect(fixture.journal.begin(fixture.attempt).hasValue(), "prepared recovery fixture");
        inspectRecovery(fixture, TransferRecoveryFinding::NoCopyObserved);
        FilesystemScanner scanner;
        QtPlanHasher hasher;
        RecoveryService legacy(fixture.history, scanner, hasher, &fixture.journal);
        const auto report = legacy.reconcile();
        expect(report && report.value().reconciled == 0 && report.value().needsAttention.size() == 1,
            "legacy recovery must not classify transfer as a failed rename");
        const auto cases = legacy.inspectCases();
        expect(cases && cases.value().size() == 1 && cases.value().front().transferTracked && !cases.value().front().canResolve,
            "legacy UI cannot resolve a three-location transfer");
        expect(!legacy.resolveCase(fixture.attempt.planId), "legacy manual resolve blocked");
        fixture.check(TransferPhase::Prepared);
    }
    {
        Fixture fixture;
        expect(fixture.journal.begin(fixture.attempt).hasValue(), "partial begin");
        expect(fixture.journal.advance(fixture.attempt.id, {TransferPhase::Prepared, TransferPhase::CopyIntent}).hasValue(), "partial intent");
        auto session = fixture.worker.open(fixture.attempt.source, fixture.attempt.sourceFingerprint, fixture.attempt.staging);
        expect(bool(session), "partial session");
        if (session) {
            expect(fixture.journal.advance(fixture.attempt.id, {TransferPhase::CopyIntent, TransferPhase::StagingCreated,
                session.value()->createdFingerprint()}).hasValue(), "partial object recorded");
            int checks{};
            expect(!session.value()->copyAndVerify([&] { return ++checks == 2; }), "partial fixture cancellation");
            session.value().reset();
            inspectRecovery(fixture, TransferRecoveryFinding::PartialCopy);
        }
    }
    {
        Fixture fixture;
        fixture.faults.failAt = TransferPhase::CommitIntent;
        expect(!fixture.useCase.execute(fixture.attempt), "verified staging fixture");
        inspectRecovery(fixture, TransferRecoveryFinding::VerifiedStaging);
        expect(std::filesystem::exists(fixture.attempt.staging) && !std::filesystem::exists(fixture.attempt.destination), "inspection does not commit staging");
    }
    {
        Fixture fixture;
        fixture.faults.beforeRecord = [&](TransferPhase phase) {
            if (phase == TransferPhase::DestinationCommitted) fixture.faults.persistent = true;
        };
        expect(!fixture.useCase.execute(fixture.attempt), "unrecorded final fixture");
        inspectRecovery(fixture, TransferRecoveryFinding::CommitNotRecorded);
        fixture.check(TransferPhase::CommitIntent);
    }
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "committed recovery fixture");
        inspectRecovery(fixture, TransferRecoveryFinding::CommittedSourceRetained);
        fixture.check(TransferPhase::DestinationCommitted);
        write(fixture.attempt.destination, "changed final content");
        inspectRecovery(fixture, TransferRecoveryFinding::ChangedFile);
    }
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "ownership fixture");
        std::filesystem::rename(fixture.attempt.destination, fixture.attempt.destination.parent_path() / "held-original-copy");
        write(fixture.attempt.destination, "original contents for transfer copy");
        inspectRecovery(fixture, TransferRecoveryFinding::ChangedFile);
        expect(std::filesystem::exists(fixture.attempt.destination.parent_path() / "held-original-copy"), "same-content different object is not deleted");
    }
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "missing source fixture");
        // 테스트가 만든 원본을 임시 폴더 안에서 보관 위치로 옮겨 '없음'만 재현한다.
        std::filesystem::rename(fixture.attempt.source, fixture.attempt.source.parent_path() / "held-source");
        inspectRecovery(fixture, TransferRecoveryFinding::Ambiguous);
        expect(fixture.journal.advance(fixture.attempt.id, {TransferPhase::DestinationCommitted,
            TransferPhase::SourceRemoveIntent}).hasValue(), "source removal intent fixture without deletion");
        inspectRecovery(fixture, TransferRecoveryFinding::SourceAbsentAfterRemoveIntent);
        expect(std::filesystem::exists(fixture.attempt.source.parent_path() / "held-source"), "inspection never removes held source");
    }
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "fault observation fixture");
        FaultInspector files;
        files.overridePath = fixture.attempt.source;
        files.replacement.presence = TransferPathPresence::Missing;
        files.replacement.volumeId = "different-volume";
        QtPlanHasher hasher;
        InspectTransferRecoveryUseCase inspect(fixture.history, fixture.journal, files, hasher);
        auto result = inspect.execute(fixture.attempt.id);
        expect(result && result.value().source == TransferRecoveryFileState::OtherVolume &&
            result.value().finding == TransferRecoveryFinding::Unavailable, "different volume not mistaken for missing source");
        files.replacement = {};
        files.replacement.error = Error{"test.offline", "Drive unavailable"};
        result = inspect.execute(fixture.attempt.id);
        expect(result && result.value().finding == TransferRecoveryFinding::Unavailable &&
            !result.value().observationErrors.empty(), "unavailable reason preserved");
        WrongHasher wrong;
        files.reads = 0;
        InspectTransferRecoveryUseCase untrusted(fixture.history, fixture.journal, files, wrong);
        result = untrusted.execute(fixture.attempt.id);
        expect(result && result.value().finding == TransferRecoveryFinding::UntrustedRecord && files.reads == 0,
            "untrusted plan does not inspect arbitrary paths");
    }
}

// 실제 SQLite 완료 트랜잭션에 실패 트리거를 설정하는 함수.
void completionFault(const QString& path, bool rollback = false) {
    const auto name = QUuid::createUuid().toString();
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", name);
        database.setDatabaseName(path);
        expect(database.open(), "completion fault DB open");
        QSqlQuery query(database);
        const auto status = rollback ? QStringLiteral("rolled_back") : QStringLiteral("completed");
        expect(query.exec(QStringLiteral("CREATE TRIGGER reject_completion BEFORE INSERT ON events WHEN NEW.event_type='plan_status_changed' AND NEW.payload_json LIKE '%") + status +
            QStringLiteral("%' BEGIN SELECT RAISE(ABORT,'injected completion failure'); END")), "completion failure trigger");
        database.close();
    }
    QSqlDatabase::removeDatabase(name);
}

// 원본 제거·실제 부재·원자적 완료와 제거 경계 실패를 실제 임시 파일로 검사하는 함수.
void testSourceRemovalAndCompletion() {
#ifdef _WIN32
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "removal copy fixture");
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        const auto result = finalize.execute(fixture.attempt.id);
        if (!result) std::cerr << result.error().code << ": " << result.error().message << '\n';
        expect(result && result.value().phase == TransferPhase::Completed, "verified source removal completes");
        expect(!std::filesystem::exists(fixture.attempt.source), "original actually absent");
        expect(TransferValidation::sameContent(fixture.attempt.sourceFingerprint, fingerprint(fixture.attempt.destination)), "final content retained");
        const auto plan = fixture.history.loadPlan(fixture.attempt.planId);
        const auto attempt = fixture.journal.load(fixture.attempt.id);
        expect(plan && plan.value().status == PlanStatus::Completed && plan.value().steps.front().status == StepStatus::Applied &&
            attempt && attempt.value().phase == TransferPhase::Completed, "transfer step plan atomically complete");
        expect(!finalize.execute(fixture.attempt.id), "completed removal cannot repeat");
    }
    {
        Fixture fixture(true);
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "batch first copy fixture");
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        expect(finalize.execute(fixture.attempt.id).hasValue(), "batch first file completes");
        const auto plan = fixture.history.loadPlan(fixture.attempt.planId);
        expect(plan && plan.value().status == PlanStatus::Executing && plan.value().steps[0].status == StepStatus::Applied &&
            plan.value().steps[1].status == StepStatus::Planned, "unfinished batch is not marked complete");
        expect(std::filesystem::exists(fixture.attempt.source.parent_path() / "pending-source"), "pending source untouched");
    }
    for (const auto failAt : {TransferPhase::SourceRemoveIntent, TransferPhase::SourceRemoved, TransferPhase::Completed}) {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "removal failure copy fixture");
        fixture.faults.failAt = failAt;
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        expect(!finalize.execute(fixture.attempt.id), "removal record failure reported");
        expect(std::filesystem::exists(fixture.attempt.source) == (failAt == TransferPhase::SourceRemoveIntent), "intent failure never removes source");
        expect(std::filesystem::exists(fixture.attempt.destination), "all removal failures retain final copy");
        const auto attempt = fixture.journal.load(fixture.attempt.id);
        expect(attempt && attempt.value().phase == TransferPhase::NeedsRecovery, "removal failure needs recovery");
        const auto plan = fixture.history.loadPlan(fixture.attempt.planId);
        expect(plan && plan.value().status == PlanStatus::Executing && plan.value().steps.front().status == StepStatus::IntentRecorded, "no false upper completion");
    }
    for (const auto scenario : {0, 1, 2, 3, 4}) {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "unsafe removal fixture");
        const auto finalBefore = fingerprint(fixture.attempt.destination);
        if (scenario == 0) write(fixture.attempt.source, "new source content");
        if (scenario == 1) write(fixture.attempt.destination, "new final content");
        if (scenario == 2) expect(SetFileAttributesW(fixture.attempt.source.c_str(), FILE_ATTRIBUTE_READONLY), "readonly source fixture");
        HANDLE reader = INVALID_HANDLE_VALUE;
        if (scenario == 3) reader = CreateFileW(fixture.attempt.source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (scenario == 4) write(fixture.attempt.staging, "unowned staging file");
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        expect(!finalize.execute(fixture.attempt.id), "unsafe source removal rejected");
        if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);
        expect(std::filesystem::exists(fixture.attempt.source) && std::filesystem::exists(fixture.attempt.destination), "unsafe removal retains both files");
        if (scenario != 1) expect(sameFingerprint(finalBefore, fingerprint(fixture.attempt.destination)), "safe final unchanged");
        if (scenario == 2) expect(SetFileAttributesW(fixture.attempt.source.c_str(), FILE_ATTRIBUTE_NORMAL), "readonly fixture cleanup");
    }
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "cancel removal fixture");
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        expect(!finalize.execute(fixture.attempt.id, [] { return true; }), "cancel before removal");
        fixture.check(TransferPhase::DestinationCommitted);
        const auto result = finalize.execute(fixture.attempt.id, [&] { return fixture.faults.last == TransferPhase::SourceRemoveIntent; });
        expect(result && result.value().phase == TransferPhase::Completed, "late cancellation cannot interrupt removal result recording");
    }
    {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "atomic completion fixture");
        completionFault(fixture.database);
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        expect(!finalize.execute(fixture.attempt.id), "real SQL completion failure exposed");
        const auto plan = fixture.history.loadPlan(fixture.attempt.planId);
        const auto attempt = fixture.journal.load(fixture.attempt.id);
        expect(plan && plan.value().status == PlanStatus::Executing && plan.value().steps.front().status == StepStatus::IntentRecorded &&
            attempt && attempt.value().phase == TransferPhase::NeedsRecovery, "SQL completion failure rolls back step and plan atomically");
        expect(!std::filesystem::exists(fixture.attempt.source) && std::filesystem::exists(fixture.attempt.destination), "post-removal SQL failure retains final copy");
    }
#endif
}

// 실제 전송 완료 기록을 가진 임시 파일만 역전송하며 실패 경계를 검사하는 함수.
void testRollbackTransfers() {
#ifdef _WIN32
    for (const auto scenario : {0, 1, 2, 3, 4, 5, 6, 7}) {
        Fixture fixture;
        expect(fixture.useCase.execute(fixture.attempt).hasValue(), "undo forward copy");
        WindowsTransferSourceRemoval removal;
        QtPlanHasher hasher;
        FinalizeTransferUseCase finalize(fixture.faults, removal, fixture.history, hasher);
        expect(finalize.execute(fixture.attempt.id).hasValue(), "undo forward completion");
        const auto moved = fingerprint(fixture.attempt.destination);
        if (scenario == 7) completionFault(fixture.database, true);
        if (scenario == 1) write(fixture.attempt.source, "new original-location file");
        if (scenario == 2) write(fixture.attempt.destination, "modified moved file");
        if (scenario == 3) fixture.faults.failAt = TransferPhase::SourceRemoveIntent;
        if (scenario == 4) fixture.faults.failAt = TransferPhase::Completed;
        if (scenario == 5) fixture.faults.beforeRecord = [&](TransferPhase phase) {
            if (phase == TransferPhase::CommitIntent) write(fixture.attempt.source, "late original-location collision");
        };
        if (scenario == 6) {
            std::filesystem::rename(fixture.attempt.destination, fixture.attempt.destination.parent_path() / "held-moved-file");
            write(fixture.attempt.destination, "original contents for transfer copy");
        }
        WindowsTransferFileInspector files;
        RollbackTransferUseCase undo(fixture.faults, fixture.history, fixture.worker, removal, files, hasher);
        const auto staging = fixture.attempt.source.parent_path() / "undo-stage";
        const auto result = undo.execute(fixture.attempt.id, "undo", staging);
        if (scenario == 0) {
            if (!result) std::cerr << result.error().code << ": " << result.error().message << '\n';
            expect(result && result.value().direction == TransferDirection::Rollback && result.value().phase == TransferPhase::Completed,
                "complete reverse transfer succeeds");
            expect(!std::filesystem::exists(fixture.attempt.destination) && std::filesystem::exists(fixture.attempt.source), "undo restores original location");
            expect(TransferValidation::sameContent(fixture.attempt.sourceFingerprint, fingerprint(fixture.attempt.source)), "undo restores original contents");
            if (result) expect(sameFingerprint(*result.value().destinationFingerprint, fingerprint(fixture.attempt.source)), "undo records new restored object identity");
            const auto plan = fixture.history.loadPlan(fixture.attempt.planId);
            expect(plan && plan.value().status == PlanStatus::RolledBack && plan.value().steps.front().status == StepStatus::RolledBack,
                "undo transfer step plan atomically rolled back");
            const auto attempts = fixture.journal.forStep(fixture.attempt.stepId);
            expect(attempts && attempts.value().size() == 2 && attempts.value()[0].direction == TransferDirection::Forward &&
                attempts.value()[1].direction == TransferDirection::Rollback, "separate forward and undo records retained");
            expect(!undo.execute(fixture.attempt.id, "second-undo", staging), "completed undo cannot repeat");
        } else {
            expect(!result, "unsafe or failed undo never succeeds");
            const auto plan = fixture.history.loadPlan(fixture.attempt.planId);
            if (scenario == 1 || scenario == 2 || scenario == 6) {
                expect(plan && plan.value().status == PlanStatus::Completed, "undo preflight rejection leaves plan unchanged");
                expect(!std::filesystem::exists(staging), "preflight rejection creates no staging");
                expect(std::filesystem::exists(fixture.attempt.destination), "preflight rejection retains moved file");
            } else {
                const auto attempt = fixture.journal.load("undo");
                expect(attempt && attempt.value().phase == TransferPhase::NeedsRecovery, "interrupted undo needs recovery");
                expect(plan && plan.value().status == PlanStatus::RollingBack && plan.value().steps.front().status == StepStatus::RollbackIntentRecorded,
                    "failed undo is not marked rolled back");
                expect(std::filesystem::exists(fixture.attempt.source), "failed undo retains restored or collision file");
                if (scenario == 3 || scenario == 5) expect(sameFingerprint(moved, fingerprint(fixture.attempt.destination)), "pre-removal failure retains moved file");
                if (scenario == 4 || scenario == 7) expect(!std::filesystem::exists(fixture.attempt.destination), "post-removal failure preserves restored file only");
            }
            if (scenario == 1 || scenario == 5) expect(fingerprint(fixture.attempt.source).sha256 ==
                hasher.sha256(scenario == 1 ? "new original-location file" : "late original-location collision"), "undo never overwrites original-location collision");
        }
    }
#endif
}
}

// 실제 SQLite와 임시 NTFS 파일의 복사·확정 연동을 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef _WIN32
    testSuccessAndCancel();
    testJournalFailures();
    testRejectedFiles();
    testReadOnlyTransferRecovery();
    testSourceRemovalAndCompletion();
    testRollbackTransfers();
#endif
    return failures ? 1 : 0;
}
