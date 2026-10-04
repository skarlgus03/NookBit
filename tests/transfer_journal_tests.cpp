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

// 명시적 복구의 예상 증거·제거 의도·이벤트 원자성을 DB만으로 검사하는 함수.
void testRecoveryCompletionContract() {
    using namespace nookbit;
    QTemporaryDir directory;
    Fixture fixture(directory.path());
    expect(fixture.ready, "recovery journal fixture");
    if (!fixture.ready) return;
    const auto dbPath = directory.filePath("history.sqlite");
    SqliteTransferJournal journal(dbPath, migrationPath());
    expect(journal.begin(fixture.attempt).hasValue(), "recovery prepared fixture");
    FileFingerprint stage{0, 1000, "target-volume", "copy-file", std::nullopt};
    expect(journal.advance("attempt", {TransferPhase::Prepared, TransferPhase::CopyIntent}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CopyIntent, TransferPhase::StagingCreated, stage}).hasValue(), "recovery staging fixture");
    stage.size = fixture.attempt.sourceFingerprint.size;
    stage.sha256 = fixture.attempt.sourceFingerprint.sha256;
    expect(journal.advance("attempt", {TransferPhase::StagingCreated, TransferPhase::CopyVerified, stage}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CopyVerified, TransferPhase::CommitIntent}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CommitIntent, TransferPhase::DestinationCommitted, {}, stage}).hasValue(), "recovery committed fixture");
    expect(journal.advance("attempt", {TransferPhase::DestinationCommitted, TransferPhase::SourceRemoveIntent}).hasValue(), "record removal intent");
    const auto expected = journal.load("attempt");
    expect(expected.hasValue(), "expected recovery state");
    if (!expected) return;
    auto forged = expected.value();
    forged.destinationFingerprint->fileId = "substituted";
    expect(!journal.reconcileCompleted(forged, fixture.plan.planHash), "recovery must compare exact final identity snapshot");
    expect(!journal.reconcileCompleted(expected.value(), std::string(64, 'f')), "recovery must compare saved approval hash");
    expect(journal.advance("attempt", {TransferPhase::SourceRemoveIntent, TransferPhase::NeedsRecovery,
        {}, {}, Error{"test.stop", "Interrupted"}}).hasValue(), "recovery phase changed fixture");
    expect(!journal.reconcileCompleted(expected.value(), fixture.plan.planHash), "stale recovery observation cannot overwrite changed phase");
    const auto current = journal.load("attempt");
    expect(current.hasValue(), "current recovery state");
    if (!current) return;
    expect(!journal.advance("attempt", {TransferPhase::NeedsRecovery, TransferPhase::Completed}), "ordinary advance cannot bypass recovery authority");
    expect(!TransferValidation::reconcileCompleted(current.value(), false), "removal intent provenance is mandatory");
    const auto count = sql(dbPath, "SELECT count(*) FROM transfer_events").toInt();
    expect(sql(dbPath, "CREATE TRIGGER fail_recovery BEFORE INSERT ON events WHEN NEW.event_type='transfer_recovery_completed' BEGIN SELECT RAISE(ABORT,'recovery event failure'); END").isValid(), "recovery audit failure fixture");
    expect(!journal.reconcileCompleted(current.value(), fixture.plan.planHash), "recovery audit failure must fail transaction");
    const auto unchanged = journal.load("attempt");
    const auto plan = fixture.history.loadPlan("plan");
    expect(unchanged && unchanged.value().phase == TransferPhase::NeedsRecovery && plan &&
        plan.value().steps[0].status == StepStatus::IntentRecorded && plan.value().status == PlanStatus::Executing &&
        sql(dbPath, "SELECT count(*) FROM transfer_events").toInt() == count, "recovery audit failure rolls back attempt step parent and events");
    expect(sql(dbPath, "DROP TRIGGER fail_recovery").isValid(), "remove recovery audit fault");
    expect(journal.reconcileCompleted(current.value(), fixture.plan.planHash).hasValue(), "explicit recovery retries after failed SQL transaction");
    SqliteTransferJournal reopened(dbPath, migrationPath());
    const auto restored = reopened.load("attempt");
    expect(restored && restored.value().phase == TransferPhase::Completed && restored.value().recoveryCompleted &&
        !restored.value().error, "recovery completion audit replays separately from errors");
    expect(!journal.reconcileCompleted(current.value(), fixture.plan.planHash), "completed recovery cannot be repeated with old snapshot");
    expect(sql(dbPath, "DELETE FROM transfer_events WHERE phase='source_remove_intent'").isValid(), "remove historical intent in corruption fixture");
    expect(!reopened.load("attempt"), "replaying a recovered completion without removal intent must reject corruption");
}

// 정리 감사 의도·결과의 순서·낡은 증거 거부·실제 SQL 실패 원자성을 검사하는 함수.
void testStagingCleanupJournal() {
    using namespace nookbit;
    QTemporaryDir directory;
    Fixture fixture(directory.path());
    if (!fixture.ready) { expect(false, "cleanup journal fixture"); return; }
    const auto dbPath = directory.filePath("history.sqlite");
    SqliteTransferJournal journal(dbPath, migrationPath());
    FileFingerprint stage{0, 1000, "target-volume", "staging-file", std::nullopt};
    expect(journal.begin(fixture.attempt).hasValue() &&
        journal.advance("attempt", {TransferPhase::Prepared, TransferPhase::CopyIntent}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CopyIntent, TransferPhase::StagingCreated, stage}).hasValue(), "cleanup initial events fixture");
    stage.size = fixture.attempt.sourceFingerprint.size; stage.sha256 = fixture.attempt.sourceFingerprint.sha256;
    expect(journal.advance("attempt", {TransferPhase::StagingCreated, TransferPhase::CopyVerified, stage}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CopyVerified, TransferPhase::Cancelled}).hasValue(), "verified cancelled copy fixture");
    const auto expected = journal.load("attempt");
    if (!expected) { expect(false, "cleanup expected evidence"); return; }
    expect(!journal.recordStagingCleanup(expected.value(), fixture.plan.planHash, StagingCleanup::Removed), "cleanup completion cannot skip durable intent");
    expect(!journal.recordStagingCleanup(expected.value(), std::string(64, 'f'), StagingCleanup::IntentRecorded), "cleanup requires current approval hash");
    auto forged = expected.value(); forged.stagingFingerprint->fileId = "other-object";
    expect(!journal.recordStagingCleanup(forged, fixture.plan.planHash, StagingCleanup::IntentRecorded), "cleanup compares exact owned object evidence");
    const auto before = sql(dbPath, "SELECT count(*) FROM transfer_events").toInt();
    expect(sql(dbPath, "CREATE TRIGGER fail_cleanup BEFORE INSERT ON events WHEN NEW.event_type='transfer_staging_cleanup' BEGIN SELECT RAISE(ABORT,'cleanup audit failure'); END").isValid(), "cleanup SQL fault fixture");
    expect(!journal.recordStagingCleanup(expected.value(), fixture.plan.planHash, StagingCleanup::IntentRecorded), "cleanup audit failure rejects intent");
    const auto unchanged = journal.load("attempt");
    expect(unchanged && unchanged.value().stagingCleanup == StagingCleanup::None &&
        sql(dbPath, "SELECT count(*) FROM transfer_events").toInt() == before, "failed cleanup intent rolls back both journals");
    expect(sql(dbPath, "DROP TRIGGER fail_cleanup").isValid(), "cleanup intent fault removed");
    expect(journal.recordStagingCleanup(expected.value(), fixture.plan.planHash, StagingCleanup::IntentRecorded).hasValue(), "cleanup intent saves before any deletion");
    expect(!journal.recordStagingCleanup(expected.value(), fixture.plan.planHash, StagingCleanup::IntentRecorded), "stale cleanup snapshot rejects duplicate intent");
    const auto intent = journal.load("attempt");
    if (!intent) { expect(false, "cleanup intent replay"); return; }
    expect(sql(dbPath, "CREATE TRIGGER fail_cleanup BEFORE INSERT ON events WHEN NEW.event_type='transfer_staging_cleanup' BEGIN SELECT RAISE(ABORT,'cleanup audit failure'); END").isValid(), "cleanup completion SQL fault fixture");
    expect(!journal.recordStagingCleanup(intent.value(), fixture.plan.planHash, StagingCleanup::Removed), "cleanup result audit failure exposed");
    const auto pending = journal.load("attempt");
    expect(pending && pending.value().stagingCleanup == StagingCleanup::IntentRecorded &&
        sql(dbPath, "SELECT count(*) FROM transfer_events").toInt() == before + 1, "cleanup result failure preserves only durable intent");
    expect(sql(dbPath, "DROP TRIGGER fail_cleanup").isValid() &&
        journal.recordStagingCleanup(intent.value(), fixture.plan.planHash, StagingCleanup::Removed).hasValue(), "cleanup result retries atomically");
    SqliteTransferJournal restarted(dbPath, migrationPath());
    const auto complete = restarted.load("attempt");
    const auto plan = fixture.history.loadPlan("plan");
    expect(complete && complete.value().phase == TransferPhase::Cancelled && complete.value().stagingCleanup == StagingCleanup::Removed &&
        plan && plan.value().status == PlanStatus::Executing && plan.value().steps[0].status == StepStatus::IntentRecorded &&
        plan.value().planHash == fixture.plan.planHash, "cleanup replay preserves movement state and approval");
    expect(sql(dbPath, "DELETE FROM transfer_events WHERE staging_cleanup=1").isValid(), "cleanup event corruption fixture");
    expect(!restarted.load("attempt"), "cleanup completion without prior intent fails replay");
}

// 복구 제거 의도의 증거·승인·원자성·재생·반복 거부를 파일 작업 없이 검사하는 함수.
void testRecoveryRemovalJournal() {
    using namespace nookbit;
    QTemporaryDir directory;
    Fixture fixture(directory.path());
    if (!fixture.ready) { expect(false, "resume journal fixture"); return; }
    const auto dbPath = directory.filePath("history.sqlite");
    SqliteTransferJournal journal(dbPath, migrationPath());
    FileFingerprint stage{123, 1000, "target-volume", "staging-file", fixture.attempt.sourceFingerprint.sha256};
    expect(journal.begin(fixture.attempt).hasValue(), "resume initial fixture");
    expect(!journal.recordRecoveryRemovalIntent(fixture.attempt, fixture.plan.planHash), "partial copies cannot request recovery removal");
    expect(journal.advance("attempt", {TransferPhase::Prepared, TransferPhase::CopyIntent}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CopyIntent, TransferPhase::StagingCreated, stage}).hasValue() &&
        journal.advance("attempt", {TransferPhase::StagingCreated, TransferPhase::CopyVerified, stage}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CopyVerified, TransferPhase::CommitIntent}).hasValue() &&
        journal.advance("attempt", {TransferPhase::CommitIntent, TransferPhase::DestinationCommitted, {}, stage}).hasValue() &&
        journal.advance("attempt", {TransferPhase::DestinationCommitted, TransferPhase::NeedsRecovery, {}, {}, Error{"test.stop", "Stopped"}}).hasValue(), "resume committed stopped fixture");
    const auto stopped = journal.load("attempt");
    if (!stopped) { expect(false, "resume stopped replay"); return; }
    auto stale = stopped.value(); stale.destinationFingerprint->fileId = "other";
    expect(!journal.recordRecoveryRemovalIntent(stale, fixture.plan.planHash), "resume compares full expected snapshot");
    expect(!journal.recordRecoveryRemovalIntent(stopped.value(), std::string(64, 'f')), "resume compares current approval hash");
    const auto before = sql(dbPath, "SELECT count(*) FROM transfer_events").toInt();
    expect(sql(dbPath, "CREATE TRIGGER fail_resume BEFORE INSERT ON events WHEN NEW.event_type='transfer_recovery_removal_intent' BEGIN SELECT RAISE(ABORT,'resume audit failure'); END").isValid(), "resume audit failure fixture");
    expect(!journal.recordRecoveryRemovalIntent(stopped.value(), fixture.plan.planHash), "resume audit failure rolls back intent");
    const auto unchanged = journal.load("attempt");
    expect(unchanged && !unchanged.value().recoveryRemovalIntent && sql(dbPath, "SELECT count(*) FROM transfer_events").toInt() == before,
        "resume failure changes neither transfer event nor snapshot");
    expect(sql(dbPath, "DROP TRIGGER fail_resume").isValid(), "resume fault cleared");
    expect(journal.recordRecoveryRemovalIntent(stopped.value(), fixture.plan.planHash).hasValue(), "resume durable intent succeeds");
    SqliteTransferJournal reopened(dbPath, migrationPath());
    const auto intent = reopened.load("attempt");
    expect(intent && intent.value().recoveryRemovalIntent && intent.value().phase == stopped.value().phase && intent.value().error->code == "test.stop",
        "resume intent preserves failure phase and diagnostic");
    expect(!journal.recordRecoveryRemovalIntent(stopped.value(), fixture.plan.planHash), "stale repeat intent rejected");
    expect(!journal.advance("attempt", {TransferPhase::NeedsRecovery, TransferPhase::DestinationCommitted}), "resume does not reopen ordinary phase transitions");
    if (!intent) return;
    expect(sql(dbPath, "UPDATE plans SET approved_at=NULL WHERE id='plan'").isValid(), "revoked approval fixture");
    expect(!journal.recordRecoveryRemovalIntent(intent.value(), fixture.plan.planHash), "pending intent cannot bypass current approval on retry");
    expect(sql(dbPath, "UPDATE plans SET approved_at='2026-10-04T00:00:00.000Z' WHERE id='plan'").isValid(), "restore approval fixture");
    expect(journal.recordRecoveryRemovalIntent(intent.value(), fixture.plan.planHash).hasValue(), "retry revalidates approval without duplicating phase event");
    expect(journal.reconcileCompleted(intent.value(), fixture.plan.planHash).hasValue(), "durable recovery removal allows explicit completion");
    const auto completed = reopened.load("attempt");
    expect(completed && completed.value().recoveryRemovalIntent && completed.value().recoveryCompleted && completed.value().phase == TransferPhase::Completed,
        "completed resume audit replays");
    expect(sql(dbPath, "DELETE FROM transfer_events WHERE phase='needs_recovery' AND recovery_removal_intent=1").isValid(), "remove resume intent event corruption fixture");
    expect(!reopened.load("attempt"), "recovery completion cannot fabricate missing removal intent");
}

// v5 기록을 보존하며 제거 의도 스키마 실패·재시도·기본값을 검사하는 함수.
void testRecoveryRemovalMigration() {
    QTemporaryDir directory;
    Fixture fixture(directory.path());
    if (!fixture.ready) { expect(false, "removal migration fixture"); return; }
    const auto dbPath = directory.filePath("history.sqlite");
    { nookbit::SqliteTransferJournal journal(dbPath, migrationPath()); expect(journal.begin(fixture.attempt).hasValue(), "v5 attempt fixture"); }
    expect(sql(dbPath, "ALTER TABLE transfer_events DROP COLUMN recovery_removal_intent").isValid() &&
        sql(dbPath, "DELETE FROM schema_migrations WHERE version=6").isValid(), "restore v5 fixture");
    const auto upgrade = directory.filePath("006_recovery_removal.sql");
    QFile broken(upgrade);
    expect(broken.open(QIODevice::WriteOnly), "removal migration fault file");
    if (!broken.isOpen()) return;
    broken.write("ALTER TABLE transfer_events ADD COLUMN recovery_removal_intent INTEGER NOT NULL DEFAULT 0; INVALID SQL;"); broken.close();
    nookbit::SqliteHistoryRepository history(dbPath, directory.filePath("001_initial.sql"));
    expect(!history.loadPlan("plan") && sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 5 &&
        sql(dbPath, "SELECT count(*) FROM pragma_table_info('transfer_events') WHERE name='recovery_removal_intent'").toInt() == 0,
        "failed v6 migration rolls back marker and column");
    expect(QFile::remove(upgrade) && QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("006_recovery_removal.sql"), upgrade), "repair v6 migration");
    const auto plan = history.loadPlan("plan");
    nookbit::SqliteTransferJournal journal(dbPath, directory.filePath("001_initial.sql"));
    const auto attempt = journal.load("attempt");
    expect(plan && plan.value().planHash == fixture.plan.planHash && attempt && !attempt.value().recoveryRemovalIntent &&
        attempt.value().phase == nookbit::TransferPhase::Prepared, "v6 preserves approval and grants no removal intent to old events");
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
               sql(dbPath, "ALTER TABLE plans DROP COLUMN format_version").isValid() &&
               sql(dbPath, "ALTER TABLE steps DROP COLUMN movement_json").isValid() &&
               sql(dbPath, "DELETE FROM schema_migrations WHERE version>=2").isValid(), "failure fixture should use v1");
    const auto migrations = directory.filePath("migrations");
    expect(QDir().mkpath(migrations), "temporary migrations directory should exist");
    const auto initial = QDir(migrations).filePath("001_initial.sql");
    const auto upgrade = QDir(migrations).filePath("002_transfer_journal.sql");
    expect(QFile::copy(migrationPath(), initial), "initial migration should copy to the fixture");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("003_plan_contract.sql"),
                      QDir(migrations).filePath("003_plan_contract.sql")), "v3 migration should copy to the fixture");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("004_transfer_recovery.sql"),
                      QDir(migrations).filePath("004_transfer_recovery.sql")), "v4 migration should copy to the fixture");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("005_staging_cleanup.sql"),
                      QDir(migrations).filePath("005_staging_cleanup.sql")), "v5 migration should copy to the fixture");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("006_recovery_removal.sql"),
                      QDir(migrations).filePath("006_recovery_removal.sql")), "v6 migration should copy to the fixture");
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
               sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 6,
           "repaired migration should succeed without losing the old plan");
}

// v3 전송 기록 보존과 복구 감사 컬럼 마이그레이션 실패의 원자성을 검사하는 함수.
void testRecoveryMigrationCompatibility() {
    QTemporaryDir directory;
    const auto dbPath = directory.filePath("history.sqlite");
    std::string oldHash;
    {
        Fixture fixture(directory.path());
        expect(fixture.ready, "recovery migration legacy fixture");
        oldHash = fixture.plan.planHash;
        nookbit::SqliteTransferJournal journal(dbPath, migrationPath());
        expect(journal.begin(fixture.attempt).hasValue(), "legacy transfer event fixture");
    }
    expect(sql(dbPath, "ALTER TABLE transfer_events DROP COLUMN recovery_completed").isValid() &&
        sql(dbPath, "ALTER TABLE transfer_events DROP COLUMN staging_cleanup").isValid() &&
        sql(dbPath, "ALTER TABLE transfer_events DROP COLUMN recovery_removal_intent").isValid() &&
        sql(dbPath, "DELETE FROM schema_migrations WHERE version>=4").isValid(), "legacy v3 database should be reconstructed");
    const auto brokenPath = directory.filePath("004_transfer_recovery.sql");
    QFile broken(brokenPath);
    expect(broken.open(QIODevice::WriteOnly), "broken recovery migration opens");
    if (!broken.isOpen()) return;
    broken.write("ALTER TABLE transfer_events ADD COLUMN recovery_completed INTEGER NOT NULL DEFAULT 0; INVALID SQL;");
    broken.close();
    nookbit::SqliteHistoryRepository upgraded(dbPath, directory.filePath("001_initial.sql"));
    expect(!upgraded.loadPlan("plan"), "invalid recovery migration rejects opening");
    expect(sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 3 &&
        sql(dbPath, "SELECT count(*) FROM pragma_table_info('transfer_events') WHERE name='recovery_completed'").toInt() == 0,
        "failed recovery migration rolls back column and version together");
    expect(QFile::remove(brokenPath) && QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("004_transfer_recovery.sql"), brokenPath),
        "recovery migration may be repaired");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("005_staging_cleanup.sql"),
        directory.filePath("005_staging_cleanup.sql")), "cleanup migration fixture should copy");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("006_recovery_removal.sql"),
        directory.filePath("006_recovery_removal.sql")), "removal migration fixture should copy");
    const auto plan = upgraded.loadPlan("plan");
    expect(plan && plan.value().planHash == oldHash, "recovery schema upgrade preserves original signature");
    nookbit::SqliteTransferJournal reopened(dbPath, directory.filePath("001_initial.sql"));
    const auto attempt = reopened.load("attempt");
    expect(attempt && attempt.value().phase == nookbit::TransferPhase::Prepared && !attempt.value().recoveryCompleted,
        "old events migrate as ordinary events without fabricating recovery");
}

// 임시 정리 스키마 이행 실패가 컬럼·버전을 남기지 않고 v4 기록을 보존하는지 검사하는 함수.
void testCleanupMigrationAtomic() {
    QTemporaryDir directory;
    const auto dbPath = directory.filePath("history.sqlite");
    std::string hash;
    {
        Fixture fixture(directory.path());
        expect(fixture.ready, "cleanup migration fixture");
        hash = fixture.plan.planHash;
        nookbit::SqliteTransferJournal journal(dbPath, migrationPath());
        expect(journal.begin(fixture.attempt).hasValue(), "cleanup migration old event fixture");
    }
    expect(sql(dbPath, "ALTER TABLE transfer_events DROP COLUMN staging_cleanup").isValid() &&
        sql(dbPath, "ALTER TABLE transfer_events DROP COLUMN recovery_removal_intent").isValid() &&
        sql(dbPath, "DELETE FROM schema_migrations WHERE version>=5").isValid(), "restore v4 fixture");
    const auto upgrade = directory.filePath("005_staging_cleanup.sql");
    QFile broken(upgrade);
    expect(broken.open(QIODevice::WriteOnly), "cleanup migration fault file");
    if (!broken.isOpen()) return;
    broken.write("ALTER TABLE transfer_events ADD COLUMN staging_cleanup INTEGER NOT NULL DEFAULT 0; INVALID SQL;"); broken.close();
    nookbit::SqliteHistoryRepository history(dbPath, directory.filePath("001_initial.sql"));
    expect(!history.loadPlan("plan") && sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 4 &&
        sql(dbPath, "SELECT count(*) FROM pragma_table_info('transfer_events') WHERE name='staging_cleanup'").toInt() == 0,
        "cleanup migration failure rolls back whole DDL and marker");
    expect(QFile::remove(upgrade) && QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("005_staging_cleanup.sql"), upgrade), "repair cleanup migration");
    expect(QFile::copy(QDir(QFileInfo(migrationPath()).path()).filePath("006_recovery_removal.sql"),
        directory.filePath("006_recovery_removal.sql")), "v6 migration should follow cleanup fixture");
    const auto plan = history.loadPlan("plan");
    nookbit::SqliteTransferJournal journal(dbPath, directory.filePath("001_initial.sql"));
    const auto old = journal.load("attempt");
    expect(plan && plan.value().planHash == hash && old && old.value().stagingCleanup == nookbit::StagingCleanup::None &&
        old.value().phase == nookbit::TransferPhase::Prepared, "v5 upgrade keeps signed v4 records without inventing cleanup");
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
               sql(dbPath, "ALTER TABLE plans DROP COLUMN format_version").isValid() &&
               sql(dbPath, "ALTER TABLE steps DROP COLUMN movement_json").isValid() &&
               sql(dbPath, "DELETE FROM schema_migrations WHERE version>=2").isValid(), "legacy v1 schema should be reconstructed");
    {
        nookbit::SqliteHistoryRepository upgraded(dbPath, migrationPath());
        const auto loaded = upgraded.loadPlan("plan");
        expect(loaded && loaded.value().planHash == oldHash && loaded.value().status == nookbit::PlanStatus::Executing,
               "v1 migration must preserve the plan hash and execution state");
        expect(sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 6,
               "v2/v3/v4/v5/v6 schema should apply exactly once");
    }
    expect(sql(dbPath, "INSERT INTO schema_migrations VALUES(7,'future')").isValid(), "future schema fixture should save");
    nookbit::SqliteHistoryRepository tooOld(dbPath, migrationPath());
    auto unsupported = tooOld.loadPlan("plan");
    expect(!unsupported && unsupported.error().code == "history.schema_too_new",
           "future schema must be rejected before writes");
    expect(!tooOld.loadPlan("plan"), "failed opening must not become usable on the next call");
    expect(sql(dbPath, "SELECT max(version) FROM schema_migrations").toInt() == 7,
           "future migration marker must remain intact");
}
} // namespace

// 전송 기록과 기존 DB 호환 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testTransferPhasesAndRoundTrip();
    testJournalFailureAndEvidence();
    testRecoveryCompletionContract();
    testStagingCleanupJournal();
    testRecoveryRemovalJournal();
    testRecoveryRemovalMigration();
    testMigrationCompatibility();
    testRecoveryMigrationCompatibility();
    testCleanupMigrationAtomic();
    testMigrationFailureIsAtomic();
    if (failures == 0) { std::cout << "All transfer journal tests passed.\n"; return 0; }
    std::cerr << failures << " transfer journal assertion(s) failed.\n";
    return 1;
}
