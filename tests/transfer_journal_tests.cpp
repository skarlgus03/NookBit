#include <nookbit/domain/transfer.hpp>
#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

namespace {
int failures = 0;

// 예상 결과와 다르면 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// 실제 마이그레이션 파일의 경로를 반환하는 함수.
QString migrationPath() {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
}

// 임시 DB에 오류 주입 SQL을 실행하거나 조회 첫 값을 읽는 함수.
QVariant sql(const QString& databasePath, const QString& statement) {
    const auto name = QUuid::createUuid().toString();
    QVariant result;
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", name);
        database.setDatabaseName(databasePath);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(statement)) result = query.next() ? query.value(0) : QVariant(true);
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

// 파일 작업 없이 기존 승인·실행 계획을 저장하는 테스트 환경.
struct Fixture {
    nookbit::QtPlanHasher hasher;
    nookbit::SqliteHistoryRepository history;
    nookbit::OperationPlan plan;
    nookbit::TransferAttempt attempt;
    bool ready{false};

    // 테스트 경로와 서명된 계획·이동 의도를 만드는 생성자.
    explicit Fixture(const QString& directory)
        : history(QDir(directory).filePath("history.sqlite"), migrationPath()) {
        plan.id = "plan";
        plan.ruleSetVersion = "rules-v3";
        plan.settingsRevision = std::string(64, 'b');
        plan.rulesRevision = std::string(64, 'c');
        plan.status = nookbit::PlanStatus::Planned;
        nookbit::MoveStep step;
        step.id = "step";
        step.source = std::filesystem::path(QDir(directory).filePath("source/file.txt").toStdWString());
        step.destination = std::filesystem::path(QDir(directory).filePath("target/file.txt").toStdWString());
        step.fingerprint = {123, 1000, "source-volume", "source-file", std::string(64, 'a')};
        step.matchedRuleId = "rule";
        plan.steps.push_back(step);
        plan.planHash = hasher.sha256(nookbit::PlanSignature::approvalPayload(plan));
        ready = history.savePlan(plan).hasValue() && history.saveApproval(
            {plan.id, plan.planHash, std::chrono::system_clock::now()}).hasValue() &&
            history.appendEvent(nookbit::PlanStatusChanged{plan.id, nookbit::PlanStatus::Executing}).hasValue() &&
            history.appendEvent(nookbit::StepStatusChanged{plan.id, step.id, nookbit::StepStatus::IntentRecorded}).hasValue();
        attempt.id = "attempt";
        attempt.planId = plan.id;
        attempt.stepId = step.id;
        attempt.source = step.source;
        attempt.destination = step.destination;
        attempt.staging = step.destination.parent_path() / L".nookbit-attempt.tmp";
        attempt.sourceFingerprint = step.fingerprint;
    }
};

// 단계 전이·내용과 객체 구분·재시작 복원·기존 승인 보존을 검사하는 함수.
void testTransferPhasesAndRoundTrip() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary fixture should exist");
    if (!directory.isValid()) return;
    Fixture fixture(directory.path());
    expect(fixture.ready, "signed legacy plan should be created");
    if (!fixture.ready) return;
    const auto dbPath = directory.filePath("history.sqlite");
    nookbit::SqliteTransferJournal journal(dbPath, migrationPath());
    expect(journal.begin(fixture.attempt).hasValue(), "prepared transfer should save");
    auto duplicate = fixture.attempt;
    duplicate.id = "duplicate";
    expect(!journal.begin(duplicate), "only one unresolved attempt may own a step");
    expect(!journal.advance("attempt", {nookbit::TransferPhase::Prepared, nookbit::TransferPhase::Completed}),
           "completion cannot skip verification or source removal phases");
    expect(journal.advance("attempt", {nookbit::TransferPhase::Prepared, nookbit::TransferPhase::CopyIntent}).hasValue(),
           "copy intent should persist");
    expect(!journal.advance("attempt", {nookbit::TransferPhase::Prepared, nookbit::TransferPhase::CopyIntent}),
           "stale expected phase cannot overwrite progress");
    nookbit::FileFingerprint staging{0, 1000, "target-volume", "staging-file", std::nullopt};
    expect(journal.advance("attempt", {nookbit::TransferPhase::CopyIntent, nookbit::TransferPhase::StagingCreated, staging}).hasValue(),
           "created staging object identity should save before copying");
    staging.size = 123;
    staging.sha256 = std::string(64, 'd');
    expect(!journal.advance("attempt", {nookbit::TransferPhase::StagingCreated, nookbit::TransferPhase::CopyVerified, staging}),
           "wrong copied content must not be verified");
    staging.sha256 = fixture.attempt.sourceFingerprint.sha256;
    expect(nookbit::TransferValidation::sameContent(staging, fixture.attempt.sourceFingerprint) &&
               !nookbit::TransferValidation::sameObject(staging, fixture.attempt.sourceFingerprint),
           "content equality must not require the source object ID on another volume");
    expect(journal.advance("attempt", {nookbit::TransferPhase::StagingCreated, nookbit::TransferPhase::CopyVerified, staging}).hasValue(),
           "verified copy evidence should save");
    nookbit::SqliteTransferJournal restarted(dbPath, migrationPath());
    auto restored = restarted.load("attempt");
    expect(restored && restored.value().phase == nookbit::TransferPhase::CopyVerified &&
               restored.value().stagingFingerprint->fileId == staging.fileId,
           "new connection should reconstruct verified copy evidence");
    expect(restarted.advance("attempt", {nookbit::TransferPhase::CopyVerified, nookbit::TransferPhase::CommitIntent}).hasValue(),
           "commit intent should persist");
    auto replaced = staging;
    replaced.fileId = "unowned-file";
    expect(!restarted.advance("attempt", {nookbit::TransferPhase::CommitIntent,
        nookbit::TransferPhase::DestinationCommitted, {}, replaced}), "matching content alone cannot claim a different final object");
    expect(restarted.advance("attempt", {nookbit::TransferPhase::CommitIntent,
        nookbit::TransferPhase::DestinationCommitted, {}, staging}).hasValue(), "final object must match the verified staging identity");
    expect(!restarted.advance("attempt", {nookbit::TransferPhase::DestinationCommitted, nookbit::TransferPhase::Cancelled}),
           "late cancellation cannot pretend the committed copy never existed");
    expect(restarted.advance("attempt", {nookbit::TransferPhase::DestinationCommitted, nookbit::TransferPhase::SourceRemoveIntent}).hasValue(),
           "source removal intent should save");
    expect(restarted.advance("attempt", {nookbit::TransferPhase::SourceRemoveIntent, nookbit::TransferPhase::SourceRemoved}).hasValue(),
           "reported source removal should save without a file API");
    expect(restarted.advance("attempt", {nookbit::TransferPhase::SourceRemoved, nookbit::TransferPhase::Completed}).hasValue(),
           "completed attempt should save");
    expect(!restarted.begin(duplicate), "completed forward transfer cannot be repeated");
    const auto loaded = fixture.history.loadPlan("plan");
    expect(loaded && loaded.value().planHash == fixture.plan.planHash &&
               loaded.value().steps.front().fingerprint.fileId == "source-file" &&
               loaded.value().steps.front().status == nookbit::StepStatus::IntentRecorded,
           "journal foundation must not rewrite the signed plan or mark a real move applied");
    expect(!QFile::exists(QString::fromStdWString(fixture.attempt.source.wstring())) &&
               !QFile::exists(QString::fromStdWString(fixture.attempt.destination.wstring())),
           "journal must never create or delete actual transfer files");
    expect(sql(dbPath, "SELECT count(*) FROM transfer_events").toInt() == 9,
           "all transfer phases should be retained as append-only events");
    expect(fixture.history.appendEvent(nookbit::StepStatusChanged{"plan", "step", nookbit::StepStatus::Applied}).hasValue() &&
               fixture.history.appendEvent(nookbit::PlanStatusChanged{"plan", nookbit::PlanStatus::Completed}).hasValue() &&
               fixture.history.appendEvent(nookbit::PlanStatusChanged{"plan", nookbit::PlanStatus::RollingBack}).hasValue() &&
               fixture.history.appendEvent(nookbit::StepStatusChanged{"plan", "step", nookbit::StepStatus::RollbackIntentRecorded}).hasValue(),
           "record-only fixture should enter rollback");
    auto undo = fixture.attempt;
    undo.id = "undo-attempt";
    undo.direction = nookbit::TransferDirection::Rollback;
    std::swap(undo.source, undo.destination);
    undo.staging = undo.destination.parent_path() / L".nookbit-undo.tmp";
    undo.sourceFingerprint = staging;
    expect(restarted.begin(undo).hasValue(), "rollback attempt should use the copied file identity, not the original ID");
    const auto attempts = restarted.forStep("step");
    expect(attempts && attempts.value().size() == 2 && attempts.value().front().id == "attempt" &&
               attempts.value().back().direction == nookbit::TransferDirection::Rollback,
           "forward and rollback attempts must remain separate and ordered");
}

// 이벤트 저장 실패의 원자성·오류 기록·경로 검증을 검사하는 함수.
void testJournalFailureAndEvidence() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "failure fixture should exist"); return; }
    Fixture fixture(directory.path());
    if (!fixture.ready) { expect(false, "failure plan should save"); return; }
    const auto dbPath = directory.filePath("history.sqlite");
    nookbit::SqliteTransferJournal journal(dbPath, migrationPath());
    auto invalid = fixture.attempt;
    invalid.destination = invalid.source;
    expect(!journal.begin(invalid), "same source and destination must fail");
    invalid = fixture.attempt;
    invalid.stepId = "other-step";
    expect(!journal.begin(invalid), "a transfer must own an existing executing step");
    expect(journal.begin(fixture.attempt).hasValue(), "valid attempt should save");
    expect(sql(dbPath, "CREATE TRIGGER inject_event_failure BEFORE INSERT ON transfer_events BEGIN SELECT RAISE(ABORT,'injected'); END").isValid(),
           "event failure trigger should install");
    expect(!journal.advance("attempt", {nookbit::TransferPhase::Prepared, nookbit::TransferPhase::CopyIntent}),
           "event failure must reject the whole update");
    const auto unchanged = journal.load("attempt");
    expect(unchanged && unchanged.value().phase == nookbit::TransferPhase::Prepared &&
               sql(dbPath, "SELECT count(*) FROM transfer_events").toInt() == 1,
           "snapshot and events must roll back together");
    expect(sql(dbPath, "DROP TRIGGER inject_event_failure").isValid(), "trigger should be removed");
    expect(journal.advance("attempt", {nookbit::TransferPhase::Prepared, nookbit::TransferPhase::NeedsRecovery,
        {}, {}, nookbit::Error{"test.uncertain", "Cannot establish ownership."}}).hasValue(), "uncertainty should preserve a review-required attempt");
    auto retry = fixture.attempt;
    retry.id = "retry";
    expect(!journal.begin(retry), "unresolved recovery must block a new attempt");
    expect(!nookbit::TransferValidation::canAdvance(static_cast<nookbit::TransferPhase>(99), nookbit::TransferPhase::NeedsRecovery),
           "unknown enum phases must be rejected");
    auto forged = fixture.attempt;
    forged.phase = nookbit::TransferPhase::SourceRemoveIntent;
    expect(!nookbit::TransferValidation::advance(forged,
        {nookbit::TransferPhase::SourceRemoveIntent, nookbit::TransferPhase::SourceRemoved}),
        "source removal cannot be recorded from a snapshot lacking verified destination evidence");
    expect(sql(dbPath, "UPDATE transfer_attempts SET phase='completed' WHERE id='attempt'").isValid(),
           "snapshot corruption fixture should apply");
    expect(!journal.load("attempt"), "snapshot/event disagreement must be rejected, not treated as success");
}

// 마이그레이션 중 SQL 실패가 부분 테이블·버전을 남기지 않고 재시도 가능한지 검사하는 함수.
void testMigrationFailureIsAtomic() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "migration failure fixture should exist"); return; }
    const auto dbPath = directory.filePath("history.sqlite");
    std::string hash;
    {
        Fixture fixture(directory.path());
        expect(fixture.ready, "migration failure original plan should save");
        hash = fixture.plan.planHash;
    }
    expect(sql(dbPath, "DROP TABLE transfer_events").isValid() && sql(dbPath, "DROP TABLE transfer_attempts").isValid() &&
               sql(dbPath, "DELETE FROM schema_migrations WHERE version=2").isValid(), "failure fixture should use v1");
    const auto migrations = directory.filePath("migrations");
    expect(QDir().mkpath(migrations), "temporary migrations directory should exist");
    const auto initial = QDir(migrations).filePath("001_initial.sql");
    const auto upgrade = QDir(migrations).filePath("002_transfer_journal.sql");
    expect(QFile::copy(migrationPath(), initial), "initial migration should copy to the fixture");
    QFile broken(upgrade);
    expect(broken.open(QIODevice::WriteOnly), "broken migration fixture should open");
    if (!broken.isOpen()) return;
    broken.write("CREATE TABLE migration_sentinel(value TEXT); INVALID SQL;");
    broken.close();
    nookbit::SqliteHistoryRepository repository(dbPath, initial);
    const auto failed = repository.loadPlan("plan");
    expect(!failed && failed.error().code == "history.migration_failed", "broken migration must reject opening");
    expect(!repository.loadPlan("plan"), "failed migration must not bypass checks on repeated calls");
    expect(sql(dbPath, "SELECT count(*) FROM sqlite_master WHERE name='migration_sentinel'").toInt() == 0 &&
               sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 1,
           "partial migration DDL and version changes must roll back");
    expect(QFile::remove(upgrade) && QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("002_transfer_journal.sql"), upgrade),
           "temporary broken migration should be replaced with the valid fixture");
    const auto repaired = repository.loadPlan("plan");
    expect(repaired && repaired.value().planHash == hash &&
               sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 2,
           "repaired migration should succeed without losing the old plan");
}

// 이전 Schema·해시 보존과 더 높은 버전 쓰기 거부를 검사하는 함수.
void testMigrationCompatibility() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "migration fixture should exist"); return; }
    const auto dbPath = directory.filePath("history.sqlite");
    std::string oldHash;
    {
        Fixture fixture(directory.path());
        expect(fixture.ready, "legacy plan fixture should save");
        oldHash = fixture.plan.planHash;
    }
    expect(sql(dbPath, "DROP TABLE transfer_events").isValid() && sql(dbPath, "DROP TABLE transfer_attempts").isValid() &&
               sql(dbPath, "DELETE FROM schema_migrations WHERE version=2").isValid(), "legacy v1 schema should be reconstructed");
    {
        nookbit::SqliteHistoryRepository upgraded(dbPath, migrationPath());
        const auto loaded = upgraded.loadPlan("plan");
        expect(loaded && loaded.value().planHash == oldHash && loaded.value().status == nookbit::PlanStatus::Executing,
               "v1 migration must preserve the plan hash and execution state");
        expect(sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 2,
               "v2 schema should apply exactly once");
    }
    expect(sql(dbPath, "INSERT INTO schema_migrations VALUES(3,'future')").isValid(), "future schema fixture should save");
    nookbit::SqliteHistoryRepository tooOld(dbPath, migrationPath());
    auto unsupported = tooOld.loadPlan("plan");
    expect(!unsupported && unsupported.error().code == "history.schema_too_new",
           "future schema must be rejected before writes");
    expect(!tooOld.loadPlan("plan"), "failed opening must not become usable on the next call");
    expect(sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 3,
           "future migration marker must remain intact");
}
} // namespace

// 전송 기록과 기존 DB 호환 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testTransferPhasesAndRoundTrip();
    testJournalFailureAndEvidence();
    testMigrationCompatibility();
    testMigrationFailureIsAtomic();
    if (failures == 0) { std::cout << "All transfer journal tests passed.\n"; return 0; }
    std::cerr << failures << " transfer journal assertion(s) failed.\n";
    return 1;
}
