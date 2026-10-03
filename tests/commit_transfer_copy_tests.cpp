#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>

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
    Fixture() {
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
}

// 실제 SQLite와 임시 NTFS 파일의 복사·확정 연동을 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef _WIN32
    testSuccessAndCancel();
    testJournalFailures();
    testRejectedFiles();
#endif
    return failures ? 1 : 0;
}
