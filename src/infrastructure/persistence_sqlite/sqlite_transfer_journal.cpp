#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"
#include "application/use_cases/plan_signature.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace nookbit {
namespace {

// 단계 값을 DB에서 안정적으로 읽고 쓸 문자열로 변환하는 함수.
QString phaseName(TransferPhase phase) {
    switch (phase) {
    case TransferPhase::Prepared: return "prepared";
    case TransferPhase::CopyIntent: return "copy_intent";
    case TransferPhase::StagingCreated: return "staging_created";
    case TransferPhase::CopyVerified: return "copy_verified";
    case TransferPhase::CommitIntent: return "commit_intent";
    case TransferPhase::DestinationCommitted: return "destination_committed";
    case TransferPhase::SourceRemoveIntent: return "source_remove_intent";
    case TransferPhase::SourceRemoved: return "source_removed";
    case TransferPhase::Completed: return "completed";
    case TransferPhase::Failed: return "failed";
    case TransferPhase::Cancelled: return "cancelled";
    case TransferPhase::NeedsRecovery: return "needs_recovery";
    }
    return {};
}

// DB의 알려진 단계만 enum으로 복원하는 함수.
std::optional<TransferPhase> parsePhase(const QString& text) {
    for (int value = 0; value <= static_cast<int>(TransferPhase::NeedsRecovery); ++value) {
        const auto phase = static_cast<TransferPhase>(value);
        if (phaseName(phase) == text) return phase;
    }
    return std::nullopt;
}

// 정밀도 손실 없이 파일 지문을 JSON으로 저장하는 함수.
QString encodeFingerprint(const FileFingerprint& fingerprint) {
    QJsonObject object{{"size", QString::number(fingerprint.size)},
                       {"modifiedUtcMs", QString::number(fingerprint.modifiedUtcMs)},
                       {"volumeId", QString::fromStdString(fingerprint.volumeId.value_or(""))},
                       {"fileId", QString::fromStdString(fingerprint.fileId.value_or(""))}};
    if (fingerprint.sha256) object.insert("sha256", QString::fromStdString(*fingerprint.sha256));
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// JSON의 숫자·객체 ID·해시를 검증하고 파일 지문을 읽는 함수.
std::optional<FileFingerprint> decodeFingerprint(const QString& json) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto object = document.object();
    bool sizeOk = false, timeOk = false;
    FileFingerprint result;
    result.size = object.value("size").toString().toULongLong(&sizeOk);
    result.modifiedUtcMs = object.value("modifiedUtcMs").toString().toLongLong(&timeOk);
    result.volumeId = object.value("volumeId").toString().toStdString();
    result.fileId = object.value("fileId").toString().toStdString();
    if (!sizeOk || !timeOk || result.volumeId->empty() || result.fileId->empty()) return std::nullopt;
    if (object.contains("sha256")) {
        result.sha256 = object.value("sha256").toString().toStdString();
        if (!PlanSignature::isSha256Hex(*result.sha256)) return std::nullopt;
    }
    return result;
}

// 없는 지문은 SQL NULL, 있는 지문은 검증 가능한 JSON으로 바꾸는 함수.
QVariant optionalFingerprint(const std::optional<FileFingerprint>& fingerprint) {
    return fingerprint ? QVariant(encodeFingerprint(*fingerprint)) : QVariant();
}

// 오류 정보를 SQL에 저장할 JSON 또는 NULL로 바꾸는 함수.
QVariant encodeError(const std::optional<Error>& error) {
    if (!error) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"code", QString::fromStdString(error->code)},
        {"message", QString::fromStdString(error->message)}}).toJson(QJsonDocument::Compact));
}

// SQL NULL의 QVariant 형식 차이와 실제 문자열 차이를 구분하는 함수.
bool sameNullable(const QVariant& stored, const QVariant& expected) {
    return stored.isNull() && expected.isNull() ? true :
        !stored.isNull() && !expected.isNull() && stored.toString() == expected.toString();
}

// 경로를 일관된 Windows/Qt 문자열로 정규화하는 함수.
QString pathText(const std::filesystem::path& path) {
    return QDir::cleanPath(QString::fromStdWString(path.wstring()));
}

// DB 경로와 시도 경로가 같은지 대소문자 차이 없이 비교하는 함수.
bool samePath(const QString& stored, const std::filesystem::path& path) {
    return QString::compare(QDir::cleanPath(stored), pathText(path), Qt::CaseInsensitive) == 0;
}

// 보호 세션이 검사한 예상 시도의 모든 불변 증거와 현재 감사 상태를 비교하는 함수.
bool sameAttempt(const TransferAttempt& actual, const TransferAttempt& expected) {
    return actual.id == expected.id && actual.planId == expected.planId && actual.stepId == expected.stepId &&
        actual.direction == expected.direction && actual.phase == expected.phase &&
        actual.recoveryCompleted == expected.recoveryCompleted && actual.stagingCleanup == expected.stagingCleanup &&
        actual.recoveryRemovalIntent == expected.recoveryRemovalIntent &&
        actual.copyResume == expected.copyResume &&
        samePath(pathText(actual.source), expected.source) && samePath(pathText(actual.destination), expected.destination) &&
        samePath(pathText(actual.staging), expected.staging) &&
        encodeFingerprint(actual.sourceFingerprint) == encodeFingerprint(expected.sourceFingerprint) &&
        sameNullable(optionalFingerprint(actual.stagingFingerprint), optionalFingerprint(expected.stagingFingerprint)) &&
        sameNullable(optionalFingerprint(actual.destinationFingerprint), optionalFingerprint(expected.destinationFingerprint)) &&
        sameNullable(encodeError(actual.error), encodeError(expected.error));
}

} // namespace

// 기존 이력과 공유할 DB 및 초기 마이그레이션 경로를 받는 생성자.
SqliteTransferJournal::SqliteTransferJournal(QString databasePath, QString migrationPath)
    : databasePath_(std::move(databasePath)), migrationPath_(std::move(migrationPath)),
      connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

// 자신이 연 연결만 닫는 소멸자.
SqliteTransferJournal::~SqliteTransferJournal() {
    database_.close();
    database_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}

// 연결을 열고 공유 Schema를 지원 버전까지 이행하는 함수.
Result<void> SqliteTransferJournal::ensureOpen() const {
    if (database_.isOpen()) return Result<void>::success();
    if (!QFileInfo(databasePath_).isAbsolute() || !QDir().mkpath(QFileInfo(databasePath_).absolutePath()))
        return Result<void>::failure({"transfer.database_path", "Cannot prepare the transfer journal location."});
    if (!database_.isValid()) database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) return Result<void>::failure({"transfer.database_open", database_.lastError().text().toStdString()});
    const auto migrated = migrateHistorySchema(database_, migrationPath_);
    if (!migrated) database_.close();
    return migrated;
}

// 현재 트랜잭션에 단계별 증거 이벤트를 추가하는 함수.
Result<void> SqliteTransferJournal::appendSnapshot(const TransferAttempt& attempt) const {
    QSqlQuery event(database_);
    event.prepare("INSERT INTO transfer_events(attempt_id, occurred_at, phase, staging_fingerprint_json, destination_fingerprint_json, error_json, recovery_completed, staging_cleanup, recovery_removal_intent,copy_resume) VALUES(?,?,?,?,?,?,?,?,?,?)");
    event.addBindValue(QString::fromStdString(attempt.id));
    event.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    event.addBindValue(phaseName(attempt.phase));
    event.addBindValue(optionalFingerprint(attempt.stagingFingerprint));
    event.addBindValue(optionalFingerprint(attempt.destinationFingerprint));
    event.addBindValue(encodeError(attempt.error));
    event.addBindValue(attempt.recoveryCompleted ? 1 : 0);
    event.addBindValue(static_cast<int>(attempt.stagingCleanup));
    event.addBindValue(attempt.recoveryRemovalIntent ? 1 : 0);
    event.addBindValue(static_cast<int>(attempt.copyResume));
    if (!event.exec()) return Result<void>::failure({"transfer.event_failed", event.lastError().text().toStdString()});
    return Result<void>::success();
}

// 실행 중인 단계에 연결된 새 시도를 기록하는 함수.
Result<void> SqliteTransferJournal::begin(const TransferAttempt& attempt) {
    const auto valid = TransferValidation::validateNew(attempt);
    if (!valid) return valid;
    const auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) return Result<void>::failure({"transfer.transaction_failed", database_.lastError().text().toStdString()});
    QSqlQuery owner(database_);
    owner.prepare("SELECT s.source_path,s.destination_path,s.fingerprint_json,s.status,p.status FROM steps s JOIN plans p ON p.id=s.plan_id WHERE s.id=? AND s.plan_id=?");
    owner.addBindValue(QString::fromStdString(attempt.stepId));
    owner.addBindValue(QString::fromStdString(attempt.planId));
    const bool forward = attempt.direction == TransferDirection::Forward;
    bool ownershipValid = owner.exec() && owner.next();
    if (ownershipValid) {
        const auto source = decodeFingerprint(owner.value(2).toString());
        ownershipValid = samePath(owner.value(forward ? 0 : 1).toString(), attempt.source) &&
            samePath(owner.value(forward ? 1 : 0).toString(), attempt.destination) && source &&
            TransferValidation::sameContent(*source, attempt.sourceFingerprint) &&
            (!forward || (TransferValidation::sameObject(*source, attempt.sourceFingerprint) &&
                source->modifiedUtcMs == attempt.sourceFingerprint.modifiedUtcMs)) &&
            owner.value(3).toString() == (forward ? "intent_recorded" : "rollback_intent_recorded") &&
            owner.value(4).toString() == (forward ? "executing" : "rolling_back");
    }
    owner.finish();
    if (!ownershipValid) {
        database_.rollback();
        return Result<void>::failure({"transfer.owner_invalid", "Attempt paths and source must match the active plan step."});
    }
    if (!forward) {
        QSqlQuery prior(database_);
        prior.prepare("SELECT id FROM transfer_attempts WHERE step_id=? AND direction='forward' AND phase='completed'");
        prior.addBindValue(QString::fromStdString(attempt.stepId));
        if (!prior.exec() || !prior.next()) {
            database_.rollback();
            return Result<void>::failure({"transfer.rollback_origin", "Completed forward copy evidence required"});
        }
        const auto priorId = prior.value(0).toString().toStdString();
        prior.finish();
        const auto original = readAttempt(priorId);
        if (!original || !original.value().destinationFingerprint ||
            !TransferValidation::sameObject(*original.value().destinationFingerprint, attempt.sourceFingerprint) ||
            !TransferValidation::sameContent(*original.value().destinationFingerprint, attempt.sourceFingerprint) ||
            original.value().destinationFingerprint->modifiedUtcMs != attempt.sourceFingerprint.modifiedUtcMs) {
            database_.rollback();
            return Result<void>::failure({"transfer.rollback_origin", "Rollback source must be the recorded forward result"});
        }
    }
    QSqlQuery existing(database_);
    existing.prepare("SELECT id FROM transfer_attempts t WHERE step_id=? AND (phase NOT IN ('completed','failed','cancelled') OR (direction=? AND phase='completed') OR EXISTS(SELECT 1 FROM transfer_events e WHERE e.attempt_id=t.id AND e.id=(SELECT max(last.id) FROM transfer_events last WHERE last.attempt_id=t.id) AND e.copy_resume<>0 AND e.staging_cleanup<>2)) LIMIT 1");
    existing.addBindValue(QString::fromStdString(attempt.stepId));
    existing.addBindValue(forward ? "forward" : "rollback");
    if (!existing.exec() || existing.next()) {
        database_.rollback();
        return Result<void>::failure({"transfer.attempt_conflict", "This step already has an unresolved or completed transfer."});
    }
    existing.finish();
    QSqlQuery insert(database_);
    insert.prepare("INSERT INTO transfer_attempts(id,plan_id,step_id,direction,source_path,destination_path,staging_path,source_fingerprint_json,phase,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    insert.addBindValue(QString::fromStdString(attempt.id));
    insert.addBindValue(QString::fromStdString(attempt.planId));
    insert.addBindValue(QString::fromStdString(attempt.stepId));
    insert.addBindValue(forward ? "forward" : "rollback");
    insert.addBindValue(pathText(attempt.source));
    insert.addBindValue(pathText(attempt.destination));
    insert.addBindValue(pathText(attempt.staging));
    insert.addBindValue(encodeFingerprint(attempt.sourceFingerprint));
    insert.addBindValue(phaseName(attempt.phase));
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    insert.addBindValue(now);
    insert.addBindValue(now);
    if (!insert.exec()) {
        const auto error = insert.lastError().text().toStdString();
        database_.rollback();
        return Result<void>::failure({"transfer.insert_failed", error});
    }
    const auto event = appendSnapshot(attempt);
    if (!event || !database_.commit()) {
        database_.rollback();
        return event ? Result<void>::failure({"transfer.commit_failed", database_.lastError().text().toStdString()}) : event;
    }
    return Result<void>::success();
}

// 이벤트를 순서대로 재생해 마지막 스냅샷을 검증·복원하는 함수.
Result<TransferAttempt> SqliteTransferJournal::load(const std::string& attemptId) const {
    const auto opened = ensureOpen();
    if (!opened) return Result<TransferAttempt>::failure(opened.error());
    if (!database_.transaction()) return Result<TransferAttempt>::failure({"transfer.transaction_failed", database_.lastError().text().toStdString()});
    const auto result = readAttempt(attemptId);
    if (!result || !database_.commit()) {
        database_.rollback();
        return result ? Result<TransferAttempt>::failure({"transfer.commit_failed", database_.lastError().text().toStdString()}) : result;
    }
    return result;
}

// 이미 열린 트랜잭션 안에서 스냅샷과 이벤트를 함께 읽는 함수.
Result<TransferAttempt> SqliteTransferJournal::readAttempt(const std::string& attemptId) const {
    QSqlQuery row(database_);
    row.prepare("SELECT plan_id,step_id,direction,source_path,destination_path,staging_path,source_fingerprint_json,phase,staging_fingerprint_json,destination_fingerprint_json,error_json FROM transfer_attempts WHERE id=?");
    row.addBindValue(QString::fromStdString(attemptId));
    if (!row.exec() || !row.next()) return Result<TransferAttempt>::failure({"transfer.not_found", "Cannot read the transfer attempt."});
    TransferAttempt result;
    result.id = attemptId;
    result.planId = row.value(0).toString().toStdString();
    result.stepId = row.value(1).toString().toStdString();
    if (row.value(2).toString() != "forward" && row.value(2).toString() != "rollback")
        return Result<TransferAttempt>::failure({"transfer.record_invalid", "Unknown transfer direction."});
    result.direction = row.value(2).toString() == "forward" ? TransferDirection::Forward : TransferDirection::Rollback;
    result.source = std::filesystem::path(row.value(3).toString().toStdWString());
    result.destination = std::filesystem::path(row.value(4).toString().toStdWString());
    result.staging = std::filesystem::path(row.value(5).toString().toStdWString());
    const auto source = decodeFingerprint(row.value(6).toString());
    if (!source) return Result<TransferAttempt>::failure({"transfer.record_invalid", "Invalid recorded source fingerprint."});
    result.sourceFingerprint = *source;
    const auto valid = TransferValidation::validateNew(result);
    if (!valid) return Result<TransferAttempt>::failure(valid.error());
    QSqlQuery events(database_);
    events.prepare("SELECT phase,staging_fingerprint_json,destination_fingerprint_json,error_json,recovery_completed,staging_cleanup,recovery_removal_intent,copy_resume FROM transfer_events WHERE attempt_id=? ORDER BY id");
    events.addBindValue(QString::fromStdString(attemptId));
    if (!events.exec() || !events.next() || events.value(0).toString() != "prepared" ||
        !events.value(1).isNull() || !events.value(2).isNull() || !events.value(3).isNull() || events.value(4).toInt() != 0 || events.value(5).toInt() != 0 || events.value(6).toInt() != 0 || events.value(7).toInt() != 0)
        return Result<TransferAttempt>::failure({"transfer.record_invalid", "Missing or invalid initial transfer event."});
    bool removalIntentRecorded = false;
    while (events.next()) {
        const auto phase = parsePhase(events.value(0).toString());
        if (!phase) return Result<TransferAttempt>::failure({"transfer.record_invalid", "Unknown transfer event phase."});
        const auto resumed = static_cast<CopyResume>(events.value(7).toInt());
        if (resumed != result.copyResume) {
            const auto verified = resumed == CopyResume::Verified ? decodeFingerprint(events.value(1).toString()) : std::nullopt;
            if (*phase != result.phase || events.value(4).toInt() != 0 || events.value(5).toInt() != 0 || events.value(6).toInt() != 0 ||
                !sameNullable(events.value(2), optionalFingerprint(result.destinationFingerprint)) ||
                !sameNullable(events.value(3), encodeError(result.error)) ||
                (resumed != CopyResume::Verified && !sameNullable(events.value(1), optionalFingerprint(result.stagingFingerprint))))
                return Result<TransferAttempt>::failure({"transfer.record_invalid", "Copy resume cannot change phase or other evidence"});
            const auto next = TransferValidation::recordCopyResume(result, resumed, verified);
            if (!next) return Result<TransferAttempt>::failure(next.error());
            result = next.value();
            continue;
        }
        const int removal = events.value(6).toInt();
        if ((removal != 0 && removal != 1) || (result.recoveryRemovalIntent && removal != 1))
            return Result<TransferAttempt>::failure({"transfer.record_invalid", "Invalid recovery removal audit"});
        if (removal == 1 && !result.recoveryRemovalIntent) {
            if (*phase != result.phase || events.value(4).toInt() != 0 || events.value(5).toInt() != 0 ||
                !sameNullable(events.value(1), optionalFingerprint(result.stagingFingerprint)) ||
                !sameNullable(events.value(2), optionalFingerprint(result.destinationFingerprint)) ||
                !sameNullable(events.value(3), encodeError(result.error)))
                return Result<TransferAttempt>::failure({"transfer.record_invalid", "Removal intent cannot change phase or evidence"});
            const auto next = TransferValidation::recordRecoveryRemovalIntent(result);
            if (!next) return Result<TransferAttempt>::failure(next.error());
            result = next.value();
            removalIntentRecorded = true;
            continue;
        }
        const auto cleanup = static_cast<StagingCleanup>(events.value(5).toInt());
        if (cleanup != StagingCleanup::None) {
            if (*phase != result.phase || events.value(4).toInt() != 0 ||
                !sameNullable(events.value(1), optionalFingerprint(result.stagingFingerprint)) ||
                !sameNullable(events.value(2), optionalFingerprint(result.destinationFingerprint)) ||
                !sameNullable(events.value(3), encodeError(result.error)))
                return Result<TransferAttempt>::failure({"transfer.record_invalid", "Cleanup event cannot change transfer phase or evidence"});
            const auto next = TransferValidation::recordStagingCleanup(result, cleanup);
            if (!next) return Result<TransferAttempt>::failure(next.error());
            result = next.value();
            continue;
        }
        if (result.stagingCleanup != StagingCleanup::None)
            return Result<TransferAttempt>::failure({"transfer.record_invalid", "A cleanup attempt cannot resume ordinary transfer events"});
        TransferUpdate update{result.phase, *phase};
        if (*phase == TransferPhase::StagingCreated || *phase == TransferPhase::CopyVerified) {
            update.stagingFingerprint = decodeFingerprint(events.value(1).toString());
            if (!update.stagingFingerprint) return Result<TransferAttempt>::failure({"transfer.record_invalid", "Invalid staging evidence."});
        } else if (!sameNullable(events.value(1), optionalFingerprint(result.stagingFingerprint))) {
            return Result<TransferAttempt>::failure({"transfer.record_invalid", "Staging evidence changed outside its phase."});
        }
        if (*phase == TransferPhase::DestinationCommitted) {
            update.destinationFingerprint = decodeFingerprint(events.value(2).toString());
            if (!update.destinationFingerprint) return Result<TransferAttempt>::failure({"transfer.record_invalid", "Invalid destination evidence."});
        } else if (!sameNullable(events.value(2), optionalFingerprint(result.destinationFingerprint))) {
            return Result<TransferAttempt>::failure({"transfer.record_invalid", "Destination evidence changed outside its phase."});
        }
        if (!events.value(3).isNull()) {
            const auto error = QJsonDocument::fromJson(events.value(3).toString().toUtf8()).object();
            update.error = Error{error.value("code").toString().toStdString(), error.value("message").toString().toStdString()};
        }
        const bool recovery = events.value(4).toInt() == 1;
        if ((events.value(4).toInt() != 0 && !recovery) || (recovery && (*phase != TransferPhase::Completed || update.error)))
            return Result<TransferAttempt>::failure({"transfer.record_invalid", "Recovery audit requires an error-free completed event"});
        const auto next = recovery ? TransferValidation::reconcileCompleted(result, removalIntentRecorded) :
            TransferValidation::advance(result, update);
        if (!next) return Result<TransferAttempt>::failure(next.error());
        if (*phase == TransferPhase::SourceRemoveIntent) removalIntentRecorded = true;
        result = next.value();
    }
    if (row.value(7).toString() != phaseName(result.phase) ||
        !sameNullable(row.value(8), optionalFingerprint(result.stagingFingerprint)) ||
        !sameNullable(row.value(9), optionalFingerprint(result.destinationFingerprint)) || !sameNullable(row.value(10), encodeError(result.error)))
        return Result<TransferAttempt>::failure({"transfer.record_invalid", "Transfer snapshot disagrees with its events."});
    return Result<TransferAttempt>::success(std::move(result));
}

// 예상 상태·증거를 검사하고 스냅샷과 이벤트를 함께 기록하는 함수.
Result<void> SqliteTransferJournal::advance(const std::string& attemptId, const TransferUpdate& update) {
    const auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) return Result<void>::failure({"transfer.transaction_failed", database_.lastError().text().toStdString()});
    const auto current = readAttempt(attemptId);
    if (!current) { database_.rollback(); return Result<void>::failure(current.error()); }
    const auto next = TransferValidation::advance(current.value(), update);
    if (!next) { database_.rollback(); return Result<void>::failure(next.error()); }
    QSqlQuery change(database_);
    change.prepare("UPDATE transfer_attempts SET phase=?,staging_fingerprint_json=?,destination_fingerprint_json=?,error_json=?,updated_at=? WHERE id=? AND phase=?");
    change.addBindValue(phaseName(next.value().phase));
    change.addBindValue(optionalFingerprint(next.value().stagingFingerprint));
    change.addBindValue(optionalFingerprint(next.value().destinationFingerprint));
    change.addBindValue(encodeError(next.value().error));
    change.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    change.addBindValue(QString::fromStdString(attemptId));
    change.addBindValue(phaseName(update.expectedPhase));
    if (!change.exec() || change.numRowsAffected() != 1) {
        database_.rollback();
        return Result<void>::failure({"transfer.phase_conflict", "Cannot update the expected transfer phase."});
    }
    const auto event = appendSnapshot(next.value());
    if (!event || !database_.commit()) {
        database_.rollback();
        return event ? Result<void>::failure({"transfer.commit_failed", database_.lastError().text().toStdString()}) : event;
    }
    return Result<void>::success();
}

// 정방향 전송·단계·필요한 계획 완료를 원자적으로 기록하는 함수.
Result<void> SqliteTransferJournal::completeForward(const std::string& attemptId) {
    return complete(attemptId, TransferDirection::Forward);
}

// 역방향 전송의 원자적 완료를 요청하는 함수.
Result<void> SqliteTransferJournal::completeRollback(const std::string& attemptId) {
    return complete(attemptId, TransferDirection::Rollback);
}

// 방향에 맞는 전송·단계·필요한 계획 완료를 원자적으로 기록하는 함수.
Result<void> SqliteTransferJournal::complete(const std::string& attemptId, TransferDirection direction,
    const TransferAttempt* recovery, const std::string& planHash) {
    const bool forward = direction == TransferDirection::Forward;
    const QString running = forward ? "executing" : "rolling_back";
    const QString intent = forward ? "intent_recorded" : "rollback_intent_recorded";
    const QString stepOutcome = forward ? "applied" : "rolled_back";
    const QString planOutcome = forward ? "completed" : "rolled_back";
    const auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) return Result<void>::failure({"transfer.transaction_failed", "Cannot start completion transaction"});
    // 모든 오류에서 동일한 완료 트랜잭션을 롤백하는 함수.
    const auto fail = [&](const char* code) {
        database_.rollback();
        return Result<void>::failure({code, "Atomic transfer/step/plan completion failed"});
    };
    const auto current = readAttempt(attemptId);
    if (!current || current.value().direction != direction) return fail("transfer.completion_state");
    if (recovery) {
        if (!sameAttempt(current.value(), *recovery)) return fail("transfer.recovery_conflict");
        QSqlQuery intentEvent(database_);
        intentEvent.prepare("SELECT 1 FROM transfer_events WHERE attempt_id=? AND (phase='source_remove_intent' OR recovery_removal_intent=1) LIMIT 1");
        intentEvent.addBindValue(QString::fromStdString(attemptId));
        if (!intentEvent.exec() || !intentEvent.next()) return fail("transfer.recovery_state");
    }
    const auto next = recovery ? TransferValidation::reconcileCompleted(current.value(), true) :
        TransferValidation::advance(current.value(), {TransferPhase::SourceRemoved, TransferPhase::Completed});
    if (!next) return fail("transfer.completion_state");
    const auto planId = QString::fromStdString(current.value().planId);
    const auto stepId = QString::fromStdString(current.value().stepId);
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery check(database_);
    check.prepare(recovery ? "SELECT status FROM plans WHERE id=? AND status IN (?,'needs_recovery') AND plan_hash=? AND approved_at IS NOT NULL" :
        "SELECT status FROM plans WHERE id=? AND status=?");
    check.addBindValue(planId);
    check.addBindValue(running);
    if (recovery) check.addBindValue(QString::fromStdString(planHash));
    if (!check.exec() || !check.next()) return fail("transfer.completion_parent");
    const auto parentStatus = check.value(0).toString();
    check.finish();
    QSqlQuery change(database_);
    change.prepare("UPDATE transfer_attempts SET phase='completed',error_json=?,updated_at=? WHERE id=? AND phase=?");
    change.addBindValue(encodeError(next.value().error));
    change.addBindValue(now); change.addBindValue(QString::fromStdString(attemptId));
    change.addBindValue(phaseName(current.value().phase));
    if (!change.exec() || change.numRowsAffected() != 1 || !appendSnapshot(next.value())) return fail("transfer.completion_event");
    QSqlQuery step(database_);
    step.prepare(recovery ? "UPDATE steps SET status=?,error_code=NULL,error_detail=NULL WHERE id=? AND plan_id=? AND status IN (?,'needs_recovery')" :
        "UPDATE steps SET status=? WHERE id=? AND plan_id=? AND status=?");
    step.addBindValue(stepOutcome);
    step.addBindValue(stepId); step.addBindValue(planId);
    step.addBindValue(intent);
    if (!step.exec() || step.numRowsAffected() != 1) return fail("transfer.completion_step");
    // 상위 이력 서비스가 사용하는 형식으로 같은 트랜잭션의 상태 이벤트를 추가하는 함수.
    const auto event = [&](const QString& type, const QString& status, bool forStep) {
        QSqlQuery insert(database_);
        insert.prepare("INSERT INTO events(plan_id,step_id,occurred_at,event_type,payload_json) VALUES(?,?,?,?,?)");
        insert.addBindValue(planId); insert.addBindValue(forStep ? QVariant(stepId) : QVariant());
        insert.addBindValue(now); insert.addBindValue(type);
        insert.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"status", status}}).toJson(QJsonDocument::Compact)));
        return insert.exec();
    };
    if (!event("step_status_changed", stepOutcome, true)) return fail("transfer.completion_event");
    if (recovery) {
        QSqlQuery audit(database_);
        audit.prepare("INSERT INTO events(plan_id,step_id,occurred_at,event_type,payload_json) VALUES(?,?,?,'transfer_recovery_completed',?)");
        audit.addBindValue(planId); audit.addBindValue(stepId); audit.addBindValue(now);
        audit.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"attemptId", QString::fromStdString(attemptId)}, {"direction", forward ? "forward" : "rollback"},
            {"status", stepOutcome}}).toJson(QJsonDocument::Compact)));
        if (!audit.exec()) return fail("transfer.completion_event");
    }
    QSqlQuery remaining(database_);
    remaining.prepare("SELECT COUNT(*) FROM steps WHERE plan_id=? AND status NOT IN (?,'skipped')");
    remaining.addBindValue(planId);
    remaining.addBindValue(stepOutcome);
    if (!remaining.exec() || !remaining.next()) return fail("transfer.completion_plan");
    if (remaining.value(0).toInt() == 0) {
        QSqlQuery unresolved(database_);
        unresolved.prepare("SELECT COUNT(*) FROM transfer_attempts WHERE plan_id=? AND phase NOT IN ('completed','failed','cancelled')");
        unresolved.addBindValue(planId);
        if (!unresolved.exec() || !unresolved.next()) return fail("transfer.completion_unresolved");
        if (unresolved.value(0).toInt() != 0) {
            if (!recovery) return fail("transfer.completion_unresolved");
            if (!database_.commit()) return fail("transfer.completion_commit");
            return Result<void>::success();
        }
        QSqlQuery plan(database_);
        plan.prepare("UPDATE plans SET status=?,completed_at=? WHERE id=? AND status=?");
        plan.addBindValue(planOutcome); plan.addBindValue(now); plan.addBindValue(planId); plan.addBindValue(parentStatus);
        if (!plan.exec() || plan.numRowsAffected() != 1 || !event("plan_status_changed", planOutcome, false)) return fail("transfer.completion_plan");
    }
    if (!database_.commit()) return fail("transfer.completion_commit");
    return Result<void>::success();
}

// 보호 검사에서 읽은 시도와 현재 DB를 대조해 명시적 복구 완료를 저장하는 함수.
Result<void> SqliteTransferJournal::reconcileCompleted(const TransferAttempt& expected, const std::string& planHash) {
    return complete(expected.id, expected.direction, &expected, planHash);
}

// 임시 정리 의도·결과와 상위 감사 이력을 저장하되 이동 상태는 바꾸지 않는 함수.
Result<void> SqliteTransferJournal::recordStagingCleanup(const TransferAttempt& expected, const std::string& planHash, StagingCleanup cleanup) {
    const auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) return Result<void>::failure({"transfer.transaction_failed", "Cannot start cleanup transaction"});
    // 정리 감사 저장 실패 전체를 롤백하는 함수.
    const auto fail = [&](const char* code) {
        database_.rollback();
        return Result<void>::failure({code, "Staging cleanup record was not committed"});
    };
    const auto current = readAttempt(expected.id);
    if (!current || !sameAttempt(current.value(), expected)) return fail("transfer.cleanup_conflict");
    const auto next = TransferValidation::recordStagingCleanup(current.value(), cleanup);
    if (!next) return fail("transfer.cleanup_state");
    const bool forward = expected.direction == TransferDirection::Forward;
    QSqlQuery owner(database_);
    owner.prepare("SELECT 1 FROM plans p JOIN steps s ON s.plan_id=p.id WHERE p.id=? AND s.id=? AND p.plan_hash=? AND p.approved_at IS NOT NULL AND p.status IN (?,'needs_recovery') AND s.status IN (?,'needs_recovery')");
    owner.addBindValue(QString::fromStdString(expected.planId)); owner.addBindValue(QString::fromStdString(expected.stepId));
    owner.addBindValue(QString::fromStdString(planHash)); owner.addBindValue(forward ? "executing" : "rolling_back");
    owner.addBindValue(forward ? "intent_recorded" : "rollback_intent_recorded");
    if (!owner.exec() || !owner.next()) return fail("transfer.cleanup_owner");
    owner.finish();
    if (!appendSnapshot(next.value())) return fail("transfer.cleanup_event");
    QSqlQuery audit(database_);
    audit.prepare("INSERT INTO events(plan_id,step_id,occurred_at,event_type,payload_json) VALUES(?,?,?,'transfer_staging_cleanup',?)");
    audit.addBindValue(QString::fromStdString(expected.planId)); audit.addBindValue(QString::fromStdString(expected.stepId));
    audit.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    audit.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"attemptId", QString::fromStdString(expected.id)},
        {"cleanup", cleanup == StagingCleanup::IntentRecorded ? "intent_recorded" : "removed"}}).toJson(QJsonDocument::Compact)));
    if (!audit.exec()) return fail("transfer.cleanup_event");
    if (!database_.commit()) return fail("transfer.cleanup_commit");
    return Result<void>::success();
}

// 중단된 확정 전송의 제거 의도와 감사 기록을 상태 변경 없이 저장하는 함수.
Result<void> SqliteTransferJournal::recordRecoveryRemovalIntent(const TransferAttempt& expected, const std::string& planHash) {
    const auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) return Result<void>::failure({"transfer.transaction_failed", "Cannot start recovery removal transaction"});
    // 제거 의도와 감사 이벤트 중 하나라도 실패하면 전체를 롤백하는 함수.
    const auto fail = [&](const char* code) {
        database_.rollback();
        return Result<void>::failure({code, "Recovery removal intent was not committed"});
    };
    const auto current = readAttempt(expected.id);
    if (!current || !sameAttempt(current.value(), expected)) return fail("transfer.resume_conflict");
    auto eligible = current.value();
    eligible.recoveryRemovalIntent = false;
    const auto next = TransferValidation::recordRecoveryRemovalIntent(eligible);
    if (!next) return fail("transfer.resume_state");
    const bool forward = expected.direction == TransferDirection::Forward;
    QSqlQuery owner(database_);
    owner.prepare("SELECT 1 FROM plans p JOIN steps s ON s.plan_id=p.id WHERE p.id=? AND s.id=? AND p.plan_hash=? AND p.approved_at IS NOT NULL AND p.status IN (?,'needs_recovery') AND s.status IN (?,'needs_recovery')");
    owner.addBindValue(QString::fromStdString(expected.planId)); owner.addBindValue(QString::fromStdString(expected.stepId));
    owner.addBindValue(QString::fromStdString(planHash)); owner.addBindValue(forward ? "executing" : "rolling_back");
    owner.addBindValue(forward ? "intent_recorded" : "rollback_intent_recorded");
    if (!owner.exec() || !owner.next()) return fail("transfer.resume_owner");
    owner.finish();
    if (!expected.recoveryRemovalIntent && !appendSnapshot(next.value())) return fail("transfer.resume_event");
    QSqlQuery audit(database_);
    audit.prepare("INSERT INTO events(plan_id,step_id,occurred_at,event_type,payload_json) VALUES(?,?,?,?,?)");
    audit.addBindValue(QString::fromStdString(expected.planId)); audit.addBindValue(QString::fromStdString(expected.stepId));
    audit.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    audit.addBindValue(expected.recoveryRemovalIntent ? "transfer_recovery_removal_retry" : "transfer_recovery_removal_intent");
    audit.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"attemptId", QString::fromStdString(expected.id)},
        {"direction", forward ? "forward" : "rollback"}}).toJson(QJsonDocument::Compact)));
    if (!audit.exec()) return fail("transfer.resume_event");
    if (!database_.commit()) return fail("transfer.resume_commit");
    return Result<void>::success();
}

// 부분 복사 의도·재시도·전체 검증을 이동 상태와 분리해 원자적으로 저장하는 함수.
Result<void> SqliteTransferJournal::recordCopyResume(const TransferAttempt& expected, const std::string& planHash,
    CopyResume resume, const std::optional<FileFingerprint>& verified) {
    const auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) return Result<void>::failure({"transfer.transaction_failed", "Cannot start copy resume transaction"});
    // 의도·스냅샷·감사 중 하나라도 실패하면 전체를 롤백하는 함수.
    const auto fail = [&](const char* code) {
        database_.rollback();
        return Result<void>::failure({code, "Copy resume record was not committed"});
    };
    const auto current = readAttempt(expected.id);
    if (!current || !sameAttempt(current.value(), expected)) return fail("transfer.copy_resume_conflict");
    const bool retry = resume == CopyResume::IntentRecorded && expected.copyResume == CopyResume::IntentRecorded;
    auto eligible = current.value();
    if (retry) eligible.copyResume = CopyResume::None;
    const auto next = TransferValidation::recordCopyResume(eligible, resume, verified);
    if (!next) return fail("transfer.copy_resume_state");
    const bool forward = expected.direction == TransferDirection::Forward;
    QSqlQuery owner(database_);
    owner.prepare("SELECT 1 FROM plans p JOIN steps s ON s.plan_id=p.id WHERE p.id=? AND s.id=? AND p.plan_hash=? AND p.approved_at IS NOT NULL AND p.status IN (?,'needs_recovery') AND s.status IN (?,'needs_recovery')");
    owner.addBindValue(QString::fromStdString(expected.planId)); owner.addBindValue(QString::fromStdString(expected.stepId));
    owner.addBindValue(QString::fromStdString(planHash)); owner.addBindValue(forward ? "executing" : "rolling_back");
    owner.addBindValue(forward ? "intent_recorded" : "rollback_intent_recorded");
    if (!owner.exec() || !owner.next()) return fail("transfer.copy_resume_owner");
    owner.finish();
    if (!retry) {
        QSqlQuery change(database_);
        change.prepare("UPDATE transfer_attempts SET staging_fingerprint_json=?,updated_at=? WHERE id=? AND phase=?");
        change.addBindValue(optionalFingerprint(next.value().stagingFingerprint));
        change.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        change.addBindValue(QString::fromStdString(expected.id)); change.addBindValue(phaseName(expected.phase));
        if (!change.exec() || change.numRowsAffected() != 1 || !appendSnapshot(next.value())) return fail("transfer.copy_resume_event");
    }
    QSqlQuery audit(database_);
    audit.prepare("INSERT INTO events(plan_id,step_id,occurred_at,event_type,payload_json) VALUES(?,?,?,'transfer_copy_resume',?)");
    audit.addBindValue(QString::fromStdString(expected.planId)); audit.addBindValue(QString::fromStdString(expected.stepId));
    audit.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    audit.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"attemptId", QString::fromStdString(expected.id)},
        {"state", retry ? "retry" : resume == CopyResume::IntentRecorded ? "intent_recorded" : "verified"}}).toJson(QJsonDocument::Compact)));
    if (!audit.exec()) return fail("transfer.copy_resume_event");
    if (!database_.commit()) return fail("transfer.copy_resume_commit");
    return Result<void>::success();
}

// 한 단계에 속한 시도를 생성 순서대로 읽는 함수.
Result<std::vector<TransferAttempt>> SqliteTransferJournal::forStep(const StepId& stepId) const {
    const auto opened = ensureOpen();
    if (!opened) return Result<std::vector<TransferAttempt>>::failure(opened.error());
    QSqlQuery rows(database_);
    rows.prepare("SELECT id FROM transfer_attempts WHERE step_id=? ORDER BY rowid");
    rows.addBindValue(QString::fromStdString(stepId));
    if (!rows.exec()) return Result<std::vector<TransferAttempt>>::failure({"transfer.read_failed", rows.lastError().text().toStdString()});
    std::vector<TransferAttempt> result;
    while (rows.next()) {
        const auto attempt = load(rows.value(0).toString().toStdString());
        if (!attempt) return Result<std::vector<TransferAttempt>>::failure(attempt.error());
        result.push_back(attempt.value());
    }
    return Result<std::vector<TransferAttempt>>::success(std::move(result));
}

} // namespace nookbit
