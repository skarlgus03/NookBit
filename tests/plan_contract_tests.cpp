#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

namespace {
int failures = 0;
const std::wstring guidA = L"\\\\?\\Volume{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}\\";
const std::wstring guidB = L"\\\\?\\Volume{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}\\";

// 실패한 예상 결과를 누적하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// 복사 기능 없이 실제 볼륨 조회만 흉내 내는 테스트 일꾼.
struct Volumes final : nookbit::ITransferVolumeInspector {
    nookbit::TransferVolume source{"3", guidA, L"NTFS", true};
    nookbit::TransferVolume destination{"5", guidB, L"NTFS", true};
    int calls{0};
    bool unavailable{false};
    // 경로별 볼륨 또는 조회 실패를 반환하는 함수.
    nookbit::Result<nookbit::TransferVolume> inspectVolume(const std::filesystem::path& path) override {
        ++calls;
        if (unavailable) return nookbit::Result<nookbit::TransferVolume>::failure({"test.unavailable", "Unavailable volume"});
        return nookbit::Result<nookbit::TransferVolume>::success(path.filename() == L"a" ? source : destination);
    }
};

// 기존 v1 승인 바이트 확인에 사용할 작은 초안을 만드는 함수.
nookbit::OperationPlan draft() {
    nookbit::OperationPlan plan;
    plan.id = "p";
    plan.ruleSetVersion = "v";
    plan.settingsRevision = std::string(64, 'b');
    plan.rulesRevision = std::string(64, 'c');
    nookbit::MoveStep step;
    step.id = "s";
    step.source = L"C:/a";
    step.destination = L"D:/b";
    step.fingerprint = {1, 2, "3", "4", {}};
    step.matchedRuleId = "r";
    step.reason = "x";
    plan.steps.push_back(step);
    return plan;
}

// 기존 v1 바이트 유지와 신규 계약의 모든 승인 필드 서명을 검사하는 함수.
void testSignatures() {
    auto plan = draft();
    const std::string golden = "nookbit-plan-contents-v11:v1:14:C:/a4:D:/b1:11:21:11:31:11:41:01:r1:x1:01:01:0";
    expect(nookbit::PlanSignature::planContents(plan) == golden, "legacy step bytes must remain exactly unchanged");
    expect(nookbit::PlanSignature::approvalPayload(plan) == "nookbit-approval-plan-v1:p:" + plan.settingsRevision +
        ":" + plan.rulesRevision + ":" + golden, "legacy approval payload must remain exactly unchanged");
    Volumes volumes;
    nookbit::PrepareTransferPlanUseCase prepare(volumes);
    const auto prepared = prepare.execute(plan);
    expect(prepared.hasValue(), "volume plan should prepare");
    if (!prepared) return;
    plan = prepared.value();
    nookbit::QtPlanHasher hasher;
    const auto hash = hasher.sha256(nookbit::PlanSignature::approvalPayload(plan));
    // 계약 필드를 하나 바꿨을 때 승인 해시도 달라지는지 검사하는 함수.
    const auto changed = [&](auto edit) {
        auto copy = plan;
        edit(*copy.steps[0].movement);
        expect(hasher.sha256(nookbit::PlanSignature::approvalPayload(copy)) != hash, "every contract field must affect approval hash");
    };
    changed([](auto& value) { value.strategy = nookbit::MoveStrategy::Rename; });
    changed([](auto& value) { value.sourceVolume.serial = "4"; });
    changed([](auto& value) { value.destinationVolume.serial = "6"; });
    changed([](auto& value) { value.sourceVolume.guidPath[11] = 'b'; });
    changed([](auto& value) { value.destinationVolume.guidPath[11] = 'a'; });
    changed([](auto& value) { value.sourceVolume.filesystem = "ReFS"; });
    changed([](auto& value) { value.destinationVolume.filesystem = "ReFS"; });
    changed([](auto& value) { value.preservation = static_cast<nookbit::PreservationPolicy>(9); });
    auto runtime = plan;
    runtime.status = nookbit::PlanStatus::Completed;
    runtime.steps[0].status = nookbit::StepStatus::Applied;
    expect(hasher.sha256(nookbit::PlanSignature::approvalPayload(runtime)) == hash, "runtime status must not change approval hash");
    plan.formatVersion = 1;
    expect(!nookbit::PlanSignature::validateFormat(plan), "contract must not silently downgrade to unsigned v1 data");
    plan.formatVersion = 9;
    expect(!nookbit::PlanSignature::validateFormat(plan), "unknown versions must fail closed");
}

// 드라이브 문자와 실제 볼륨이 다를 때도 정확한 전략을 선택하고 교체를 거부하는 함수.
void testVolumeContracts() {
    Volumes volumes;
    nookbit::PrepareTransferPlanUseCase prepare(volumes);
    auto input = draft();
    input.steps[0].destination = L"C:/b";
    auto plan = prepare.execute(input);
    expect(plan && plan.value().steps[0].movement->strategy == nookbit::MoveStrategy::VerifiedCopy,
           "same drive letter is not proof of the same volume");
    if (!plan) return;
    expect(prepare.verify(plan.value()).hasValue(), "unchanged current volumes should verify");
    volumes.destination.name = guidA;
    expect(!prepare.verify(plan.value()), "GUID replacement must fail even if serial is unchanged");
    volumes.destination = volumes.source;
    input.steps[0].destination = L"D:/b";
    auto renamed = prepare.execute(input);
    expect(renamed && renamed.value().steps[0].movement->strategy == nookbit::MoveStrategy::Rename,
           "different letters for the same GUID should use rename");
    volumes.source.id = "4";
    expect(!prepare.execute(input), "scanner fingerprint must agree with source volume");
    volumes.source.id = "3";
    volumes.destination.localNtfs = false;
    expect(!prepare.execute(input), "unsupported volumes must not produce approvable contracts");
    volumes.destination.localNtfs = true;
    volumes.destination.name = L"not-a-guid";
    expect(!prepare.execute(input), "malformed volume names must be rejected");
    volumes.unavailable = true;
    expect(!prepare.verify(plan.value()), "unavailable volumes must not be considered unchanged");
    const auto before = volumes.calls;
    input.status = nookbit::PlanStatus::Approved;
    expect(!prepare.execute(input) && volumes.calls == before, "approved plans must never be rewritten or probed for upgrade");
}

// 테스트 DB에 계약 변조를 주입하는 함수.
bool mutate(const QString& path, const QString& statement) {
    const auto name = QUuid::createUuid().toString();
    bool result = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (db.open()) { QSqlQuery query(db); result = query.exec(statement); }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

// v2 계약의 SQLite 재시작 복원·변조 검출·알 수 없는 형식 거부를 검사하는 함수.
void testPersistence() {
    QTemporaryDir directory;
    expect(directory.isValid(), "database test directory should exist");
    if (!directory.isValid()) return;
    const auto path = directory.filePath("history.sqlite");
    const auto migration = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql";
    Volumes volumes;
    nookbit::PrepareTransferPlanUseCase prepare(volumes);
    auto result = prepare.execute(draft());
    if (!result) { expect(false, "persistence plan should prepare"); return; }
    auto plan = result.value();
    plan.status = nookbit::PlanStatus::Planned;
    plan.steps[0].fingerprint.sha256 = std::string(64, 'a');
    nookbit::QtPlanHasher hasher;
    plan.planHash = hasher.sha256(nookbit::PlanSignature::approvalPayload(plan));
    {
        nookbit::SqliteHistoryRepository history(path, migration);
        expect(history.savePlan(plan).hasValue(), "v2 plan should persist");
    }
    nookbit::SqliteHistoryRepository restarted(path, migration);
    auto loaded = restarted.loadPlan(plan.id);
    expect(loaded && loaded.value().formatVersion == 2 &&
        nookbit::PlanSignature::approvalPayload(loaded.value()) == nookbit::PlanSignature::approvalPayload(plan),
        "v2 round trip must preserve every signed field");
    expect(mutate(path, "UPDATE steps SET movement_json=json_set(movement_json,'$.destinationVolume.serial','6')"), "tamper should apply");
    loaded = restarted.loadPlan(plan.id);
    expect(loaded && hasher.sha256(nookbit::PlanSignature::approvalPayload(loaded.value())) != loaded.value().planHash,
           "valid-looking volume tamper must invalidate signature");
    expect(mutate(path, "UPDATE steps SET movement_json=json_set(movement_json,'$.strategy',0.5)"), "fractional enum tamper should apply");
    expect(!restarted.loadPlan(plan.id), "fractional enums must not truncate to valid values");
    expect(mutate(path, "UPDATE steps SET movement_json=NULL"), "missing contract tamper should apply");
    expect(!restarted.loadPlan(plan.id), "v2 missing contract must not become v1");
    expect(mutate(path, "UPDATE plans SET format_version=99"), "unknown version tamper should apply");
    expect(!restarted.loadPlan(plan.id), "unknown plan versions must be rejected");
}

// 신규 계약 마이그레이션 실패 시 부분 컬럼을 롤백하고 기존 서명을 유지하는 함수.
void testContractMigrationFailure() {
    QTemporaryDir directory;
    expect(directory.isValid(), "migration fixture directory should exist");
    if (!directory.isValid()) return;
    const auto path = directory.filePath("legacy.sqlite");
    const auto sourceDir = QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/";
    auto plan = draft();
    plan.status = nookbit::PlanStatus::Planned;
    nookbit::QtPlanHasher hasher;
    plan.planHash = hasher.sha256(nookbit::PlanSignature::approvalPayload(plan));
    {
        nookbit::SqliteHistoryRepository original(path, sourceDir + "001_initial.sql");
        expect(original.savePlan(plan).hasValue(), "legacy plan should save");
    }
    expect(mutate(path, "ALTER TABLE plans DROP COLUMN format_version") &&
        mutate(path, "ALTER TABLE steps DROP COLUMN movement_json") &&
        mutate(path, "ALTER TABLE transfer_events DROP COLUMN recovery_completed") &&
        mutate(path, "ALTER TABLE transfer_events DROP COLUMN staging_cleanup") &&
        mutate(path, "ALTER TABLE transfer_events DROP COLUMN recovery_removal_intent") &&
        mutate(path, "DELETE FROM schema_migrations WHERE version>=3"), "legacy v2 database should be reconstructed");
    const auto initial = directory.filePath("001_initial.sql");
    const auto upgrade = directory.filePath("003_plan_contract.sql");
    QFile broken(upgrade);
    expect(broken.open(QIODevice::WriteOnly), "broken contract migration should open");
    if (!broken.isOpen()) return;
    broken.write("ALTER TABLE plans ADD COLUMN format_version INTEGER NOT NULL DEFAULT 1; INVALID SQL;");
    broken.close();
    nookbit::SqliteHistoryRepository repository(path, initial);
    auto loaded = repository.loadPlan(plan.id);
    expect(!loaded && loaded.error().code == "history.migration_failed", "partial contract migration must fail");
    expect(!repository.loadPlan(plan.id), "failed migration must not bypass validation on retry");
    expect(mutate(path, "ALTER TABLE plans ADD COLUMN format_version INTEGER NOT NULL DEFAULT 1") &&
        mutate(path, "ALTER TABLE plans DROP COLUMN format_version"), "failed migration must leave no partial format column");
    expect(QFile::remove(upgrade) && QFile::copy(sourceDir + "003_plan_contract.sql", upgrade), "valid contract migration should replace failed fixture");
    expect(QFile::copy(sourceDir + "004_transfer_recovery.sql", directory.filePath("004_transfer_recovery.sql")), "recovery migration fixture should copy");
    expect(QFile::copy(sourceDir + "005_staging_cleanup.sql", directory.filePath("005_staging_cleanup.sql")), "cleanup migration fixture should copy");
    expect(QFile::copy(sourceDir + "006_recovery_removal.sql", directory.filePath("006_recovery_removal.sql")), "removal recovery migration fixture should copy");
    loaded = repository.loadPlan(plan.id);
    expect(loaded && loaded.value().formatVersion == 1 && loaded.value().planHash == plan.planHash &&
        nookbit::PlanSignature::approvalPayload(loaded.value()) == nookbit::PlanSignature::approvalPayload(plan),
        "retry must preserve legacy version, signature bytes and saved hash");
}
}

// 계획 형식·볼륨 재검증·저장 호환 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testSignatures();
    testVolumeContracts();
    testPersistence();
    testContractMigrationFailure();
    return failures == 0 ? 0 : 1;
}
