#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"
#include "application/use_cases/plan_signature.hpp"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <set>
#include <chrono>
#include <type_traits>
#include <utility>

namespace nookbit {
namespace {

// SQL 오류를 코어 결과의 오류 형식으로 변환하는 함수.
Result<void> sqlFailure(const char* code, const QSqlQuery& query) {
    return Result<void>::failure({code, query.lastError().text().toStdString()});
}

// 경로를 SQLite에 저장할 유니코드 문자열로 변환하는 함수.
QString pathString(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

// 계획 상태를 DB의 고정된 문자열로 변환하는 함수.
QString planStatus(PlanStatus status) {
    switch (status) {
    case PlanStatus::Draft: return QStringLiteral("draft");
    case PlanStatus::Planned: return QStringLiteral("planned");
    case PlanStatus::AwaitingApproval: return QStringLiteral("awaiting_approval");
    case PlanStatus::Approved: return QStringLiteral("approved");
    case PlanStatus::Executing: return QStringLiteral("executing");
    case PlanStatus::Completed: return QStringLiteral("completed");
    case PlanStatus::RollingBack: return QStringLiteral("rolling_back");
    case PlanStatus::RolledBack: return QStringLiteral("rolled_back");
    case PlanStatus::NeedsRecovery: return QStringLiteral("needs_recovery");
    case PlanStatus::Failed: return QStringLiteral("failed");
    case PlanStatus::Cancelled: return QStringLiteral("cancelled");
    }
    return {};
}

// DB의 계획 상태 문자열을 코어 값으로 복원하는 함수.
std::optional<PlanStatus> parsePlanStatus(const QString& text) {
    for (int value = static_cast<int>(PlanStatus::Draft);
         value <= static_cast<int>(PlanStatus::Cancelled); ++value) {
        const auto candidate = static_cast<PlanStatus>(value);
        if (planStatus(candidate) == text) return candidate;
    }
    return std::nullopt;
}

// 단계 상태를 DB의 고정된 문자열로 변환하는 함수.
QString stepStatus(StepStatus status) {
    switch (status) {
    case StepStatus::Planned: return QStringLiteral("planned");
    case StepStatus::Skipped: return QStringLiteral("skipped");
    case StepStatus::IntentRecorded: return QStringLiteral("intent_recorded");
    case StepStatus::Applied: return QStringLiteral("applied");
    case StepStatus::RollbackIntentRecorded: return QStringLiteral("rollback_intent_recorded");
    case StepStatus::RolledBack: return QStringLiteral("rolled_back");
    case StepStatus::NeedsRecovery: return QStringLiteral("needs_recovery");
    case StepStatus::Failed: return QStringLiteral("failed");
    }
    return {};
}

// DB의 단계 상태 문자열을 코어 값으로 복원하는 함수.
std::optional<StepStatus> parseStepStatus(const QString& text) {
    for (int value = static_cast<int>(StepStatus::Planned);
         value <= static_cast<int>(StepStatus::Failed); ++value) {
        const auto candidate = static_cast<StepStatus>(value);
        if (stepStatus(candidate) == text) return candidate;
    }
    return std::nullopt;
}

// 충돌 정책을 DB의 고정된 문자열로 변환하는 함수.
QString collisionPolicy(CollisionPolicy policy) {
    switch (policy) {
    case CollisionPolicy::Rename: return QStringLiteral("rename");
    case CollisionPolicy::Skip: return QStringLiteral("skip");
    case CollisionPolicy::Abort: return QStringLiteral("abort");
    }
    return {};
}

// DB의 충돌 정책 문자열을 코어 값으로 복원하는 함수.
std::optional<CollisionPolicy> parseCollisionPolicy(const QString& text) {
    if (text == QStringLiteral("rename")) return CollisionPolicy::Rename;
    if (text == QStringLiteral("skip")) return CollisionPolicy::Skip;
    if (text == QStringLiteral("abort")) return CollisionPolicy::Abort;
    return std::nullopt;
}

// 파일 지문을 정밀도 손실 없이 JSON 문자열로 만드는 함수.
QString fingerprintJson(const FileFingerprint& fingerprint) {
    QJsonObject object;
    object.insert(QStringLiteral("size"), QString::number(fingerprint.size));
    object.insert(QStringLiteral("modifiedUtcMs"), QString::number(fingerprint.modifiedUtcMs));
    if (fingerprint.volumeId) object.insert(QStringLiteral("volumeId"),
                                              QString::fromStdString(*fingerprint.volumeId));
    if (fingerprint.fileId) object.insert(QStringLiteral("fileId"),
                                            QString::fromStdString(*fingerprint.fileId));
    if (fingerprint.sha256) object.insert(QStringLiteral("sha256"),
                                           QString::fromStdString(*fingerprint.sha256));
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// JSON 문자열에서 파일 지문을 복원하는 함수.
std::optional<FileFingerprint> parseFingerprint(const QString& text) {
    const auto object = QJsonDocument::fromJson(text.toUtf8()).object();
    bool sizeOk = false;
    bool modifiedOk = false;
    FileFingerprint fingerprint;
    fingerprint.size = object.value(QStringLiteral("size")).toString().toULongLong(&sizeOk);
    fingerprint.modifiedUtcMs = object.value(QStringLiteral("modifiedUtcMs"))
                                    .toString().toLongLong(&modifiedOk);
    if (!sizeOk || !modifiedOk) return std::nullopt;
    if (object.contains(QStringLiteral("volumeId")))
        fingerprint.volumeId = object.value(QStringLiteral("volumeId")).toString().toStdString();
    if (object.contains(QStringLiteral("fileId")))
        fingerprint.fileId = object.value(QStringLiteral("fileId")).toString().toStdString();
    if (object.contains(QStringLiteral("sha256")))
        fingerprint.sha256 = object.value(QStringLiteral("sha256")).toString().toStdString();
    return fingerprint;
}

// 승인한 볼륨 식별 정보를 JSON 객체로 만드는 함수.
QJsonObject volumeJson(const PlanVolume& volume) {
    return {{"serial", QString::fromStdString(volume.serial)},
            {"guidPath", QString::fromStdString(volume.guidPath)},
            {"filesystem", QString::fromStdString(volume.filesystem)}};
}

// 이동 계약을 DB에 저장할 JSON 문자열로 만드는 함수.
QString movementJson(const MoveContract& movement) {
    QJsonObject object{{"strategy", static_cast<int>(movement.strategy)},
                       {"sourceVolume", volumeJson(movement.sourceVolume)},
                       {"destinationVolume", volumeJson(movement.destinationVolume)},
                       {"preservation", static_cast<int>(movement.preservation)}};
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// 볼륨 JSON의 필수 문자열과 알 수 없는 필드를 검사해 복원하는 함수.
std::optional<PlanVolume> parseVolume(const QJsonValue& value) {
    if (!value.isObject()) return {};
    const auto object = value.toObject();
    if (object.size() != 3 || !object.value("serial").isString() ||
        !object.value("guidPath").isString() || !object.value("filesystem").isString()) return {};
    return PlanVolume{object.value("serial").toString().toStdString(),
                      object.value("guidPath").toString().toStdString(),
                      object.value("filesystem").toString().toStdString()};
}

// 누락·알 수 없는 enum·소수 값이 있는 이동 계약을 거부하는 함수.
std::optional<MoveContract> parseMovement(const QString& text) {
    const auto document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject()) return {};
    const auto object = document.object();
    const auto strategy = object.value("strategy");
    const auto preservation = object.value("preservation");
    const auto source = parseVolume(object.value("sourceVolume"));
    const auto destination = parseVolume(object.value("destinationVolume"));
    if (object.size() != 4 || !strategy.isDouble() ||
        (strategy.toDouble() != 0 && strategy.toDouble() != 1) ||
        !preservation.isDouble() || preservation.toDouble() != 0 || !source || !destination) return {};
    return MoveContract{static_cast<MoveStrategy>(strategy.toInt()), *source, *destination,
                        PreservationPolicy::LocalNtfsBasic};
}

// 경고 목록을 DB의 JSON 문자열로 만드는 함수.
QString warningsJson(const std::vector<Error>& warnings) {
    QJsonArray array;
    for (const auto& warning : warnings) {
        QJsonObject object;
        object.insert(QStringLiteral("code"), QString::fromStdString(warning.code));
        object.insert(QStringLiteral("message"), QString::fromStdString(warning.message));
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// DB의 JSON 문자열에서 경고 목록을 복원하는 함수.
std::optional<std::vector<Error>> parseWarnings(const QString& text) {
    const auto document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) return std::nullopt;
    std::vector<Error> warnings;
    for (const auto& value : document.array()) {
        if (!value.isObject()) return std::nullopt;
        const auto object = value.toObject();
        warnings.push_back({object.value(QStringLiteral("code")).toString().toStdString(),
                            object.value(QStringLiteral("message")).toString().toStdString()});
    }
    return warnings;
}

} // namespace

// DB 파일과 초기 Schema 경로를 보관하는 생성자.
SqliteHistoryRepository::SqliteHistoryRepository(QString databasePath, QString migrationPath)
    : databasePath_(std::move(databasePath)), migrationPath_(std::move(migrationPath)),
      connectionName_(QStringLiteral("nookbit-history-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

// 자신이 연 DB 연결을 닫는 소멸자.
SqliteHistoryRepository::~SqliteHistoryRepository() {
    if (!database_.isValid()) return;
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

// 데이터베이스를 열고 초기 Schema를 적용하는 함수.
Result<void> SqliteHistoryRepository::ensureOpen() const {
    if (database_.isOpen()) return Result<void>::success();
    if (databasePath_.isEmpty() || !QFileInfo(databasePath_).isAbsolute()) {
        return Result<void>::failure({"history.path_invalid", "Database path must be absolute."});
    }
    if (!QDir().mkpath(QFileInfo(databasePath_).absolutePath())) {
        return Result<void>::failure({"history.directory_failed", "Cannot create app-data directory."});
    }
    if (!database_.isValid()) database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        return Result<void>::failure({"history.open_failed", database_.lastError().text().toStdString()});
    }

    const auto migrated = migrateHistorySchema(database_, migrationPath_);
    if (!migrated) database_.close();
    return migrated;
}

// Planned 계획과 단계들을 한 트랜잭션에 저장하는 함수.
Result<void> SqliteHistoryRepository::savePlan(const OperationPlan& plan) {
    const auto format = PlanSignature::validateFormat(plan);
    if (!format) return format;
    if (plan.status != PlanStatus::Planned || plan.id.empty() || plan.planHash.size() != 64 ||
        plan.settingsRevision.size() != 64 || plan.rulesRevision.size() != 64 ||
        std::none_of(plan.steps.begin(), plan.steps.end(), [](const MoveStep& step) {
            return step.status == StepStatus::Planned;
        })) {
        return Result<void>::failure({"history.plan_invalid", "Only a hashed Planned plan can be saved."});
    }
    auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) {
        return Result<void>::failure({"history.transaction_failed", database_.lastError().text().toStdString()});
    }

    QSqlQuery planQuery(database_);
    planQuery.prepare(QStringLiteral(
        "INSERT INTO plans(id, created_at, rule_set_version, settings_revision, rules_revision, "
        "plan_hash, status, warnings_json, format_version) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    planQuery.addBindValue(QString::fromStdString(plan.id));
    planQuery.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    planQuery.addBindValue(QString::fromStdString(plan.ruleSetVersion));
    planQuery.addBindValue(QString::fromStdString(plan.settingsRevision));
    planQuery.addBindValue(QString::fromStdString(plan.rulesRevision));
    planQuery.addBindValue(QString::fromStdString(plan.planHash));
    planQuery.addBindValue(planStatus(plan.status));
    planQuery.addBindValue(warningsJson(plan.warnings));
    planQuery.addBindValue(plan.formatVersion);
    if (!planQuery.exec()) {
        auto error = sqlFailure("history.insert_plan_failed", planQuery);
        database_.rollback();
        return error;
    }

    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
        const auto& step = plan.steps[index];
        QSqlQuery stepQuery(database_);
        stepQuery.prepare(QStringLiteral(
            "INSERT INTO steps(id, plan_id, sequence_no, source_path, destination_path, "
            "fingerprint_json, matched_rule_id, reason, collision_policy, status, movement_json) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        stepQuery.addBindValue(QString::fromStdString(step.id));
        stepQuery.addBindValue(QString::fromStdString(plan.id));
        stepQuery.addBindValue(static_cast<qlonglong>(index));
        stepQuery.addBindValue(pathString(step.source));
        stepQuery.addBindValue(pathString(step.destination));
        stepQuery.addBindValue(fingerprintJson(step.fingerprint));
        stepQuery.addBindValue(QString::fromStdString(step.matchedRuleId));
        stepQuery.addBindValue(QString::fromStdString(step.reason));
        stepQuery.addBindValue(collisionPolicy(step.collisionPolicy));
        stepQuery.addBindValue(stepStatus(step.status));
        stepQuery.addBindValue(step.movement ? movementJson(*step.movement) : QString());
        if (!stepQuery.exec()) {
            auto error = sqlFailure("history.insert_step_failed", stepQuery);
            database_.rollback();
            return error;
        }
    }
    if (!database_.commit()) {
        database_.rollback();
        return Result<void>::failure({"history.commit_failed", database_.lastError().text().toStdString()});
    }
    return Result<void>::success();
}

// 해시가 같은 Planned 계획 하나를 Approved로 기록하는 함수.
Result<void> SqliteHistoryRepository::saveApproval(const ApprovalRecord& approval) {
    if (approval.planId.empty() || approval.planHash.size() != 64 ||
        approval.approvedAt == std::chrono::system_clock::time_point{}) {
        return Result<void>::failure({"history.approval_invalid", "Approval record is incomplete."});
    }
    auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) {
        return Result<void>::failure({"history.transaction_failed", database_.lastError().text().toStdString()});
    }
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE plans SET status = 'approved', approved_at = ? "
        "WHERE id = ? AND plan_hash = ? AND status = 'planned'"));
    const auto approvedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        approval.approvedAt.time_since_epoch()).count();
    update.addBindValue(QDateTime::fromMSecsSinceEpoch(approvedMs, Qt::UTC)
                            .toString(Qt::ISODateWithMs));
    update.addBindValue(QString::fromStdString(approval.planId));
    update.addBindValue(QString::fromStdString(approval.planHash));
    if (!update.exec() || update.numRowsAffected() != 1) {
        auto error = update.lastError().isValid()
            ? sqlFailure("history.approval_failed", update)
            : Result<void>::failure({"history.approval_conflict", "Plan was not Planned with this hash."});
        database_.rollback();
        return error;
    }
    QSqlQuery event(database_);
    event.prepare(QStringLiteral(
        "INSERT INTO events(plan_id, occurred_at, event_type, payload_json) "
        "VALUES(?, ?, 'plan_approved', '{}')"));
    event.addBindValue(QString::fromStdString(approval.planId));
    event.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!event.exec()) {
        auto error = sqlFailure("history.event_failed", event);
        database_.rollback();
        return error;
    }
    if (!database_.commit()) {
        database_.rollback();
        return Result<void>::failure({"history.commit_failed", database_.lastError().text().toStdString()});
    }
    return Result<void>::success();
}

// 실행 상태 변경과 이벤트를 한 트랜잭션에 기록하는 함수.
Result<void> SqliteHistoryRepository::appendEvent(const DomainEvent& event) {
    auto opened = ensureOpen();
    if (!opened) return opened;
    if (!database_.transaction()) {
        return Result<void>::failure({"history.transaction_failed", database_.lastError().text().toStdString()});
    }

    QString planId;
    QString stepId;
    QString eventType;
    QJsonObject payload;
    QString updateSql;
    QString newStatus;
    QString previousStatus;
    bool fixedPreviousStates = false;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PlanCreated>) {
            planId = QString::fromStdString(value.planId);
            eventType = QStringLiteral("plan_created");
        } else if constexpr (std::is_same_v<T, PlanStatusChanged>) {
            planId = QString::fromStdString(value.planId);
            eventType = QStringLiteral("plan_status_changed");
            newStatus = planStatus(value.status);
            payload.insert(QStringLiteral("status"), newStatus);
            updateSql = QStringLiteral("UPDATE plans SET status = ? WHERE id = ? AND status = ?");
            switch (value.status) {
            case PlanStatus::Executing: previousStatus = QStringLiteral("approved"); break;
            case PlanStatus::Completed:
                updateSql = QStringLiteral(
                    "UPDATE plans SET status = ? WHERE id = ? "
                    "AND status IN ('executing','rolling_back','needs_recovery')");
                fixedPreviousStates = true;
                break;
            case PlanStatus::RollingBack:
                updateSql = QStringLiteral(
                    "UPDATE plans SET status = ? WHERE id = ? AND status IN ('completed','failed','executing')");
                fixedPreviousStates = true;
                break;
            case PlanStatus::RolledBack:
                updateSql = QStringLiteral(
                    "UPDATE plans SET status = ? WHERE id = ? "
                    "AND status IN ('rolling_back','needs_recovery')");
                fixedPreviousStates = true;
                break;
            case PlanStatus::Failed:
                updateSql = QStringLiteral(
                    "UPDATE plans SET status = ? WHERE id = ? "
                    "AND status IN ('executing','rolling_back','needs_recovery')");
                fixedPreviousStates = true;
                break;
            case PlanStatus::NeedsRecovery:
                updateSql = QStringLiteral(
                    "UPDATE plans SET status = ? WHERE id = ? AND status IN ('executing','rolling_back')");
                fixedPreviousStates = true;
                break;
            default: break;
            }
        } else if constexpr (std::is_same_v<T, StepStatusChanged>) {
            planId = QString::fromStdString(value.planId);
            stepId = QString::fromStdString(value.stepId);
            eventType = QStringLiteral("step_status_changed");
            newStatus = stepStatus(value.status);
            payload.insert(QStringLiteral("status"), newStatus);
            updateSql = QStringLiteral(
                "UPDATE steps SET status = ? WHERE id = ? AND plan_id = ? AND status = ?");
            switch (value.status) {
            case StepStatus::IntentRecorded: previousStatus = QStringLiteral("planned"); break;
            case StepStatus::Applied:
                updateSql = QStringLiteral(
                    "UPDATE steps SET status = ? WHERE id = ? AND plan_id = ? "
                    "AND status IN ('intent_recorded','rollback_intent_recorded','needs_recovery')");
                fixedPreviousStates = true;
                break;
            case StepStatus::RollbackIntentRecorded: previousStatus = QStringLiteral("applied"); break;
            case StepStatus::RolledBack:
                updateSql = QStringLiteral(
                    "UPDATE steps SET status = ? WHERE id = ? AND plan_id = ? "
                    "AND status IN ('rollback_intent_recorded','needs_recovery')");
                fixedPreviousStates = true;
                break;
            case StepStatus::Failed:
                updateSql = QStringLiteral(
                    "UPDATE steps SET status = ? WHERE id = ? AND plan_id = ? "
                    "AND status IN ('intent_recorded','needs_recovery')");
                fixedPreviousStates = true;
                break;
            case StepStatus::NeedsRecovery:
                updateSql = QStringLiteral(
                    "UPDATE steps SET status = ? WHERE id = ? AND plan_id = ? "
                    "AND status IN ('intent_recorded','rollback_intent_recorded','applied','rolled_back','failed','planned')");
                fixedPreviousStates = true;
                break;
            default: break;
            }
        } else if constexpr (std::is_same_v<T, HomeStateChanged>) {
            eventType = QStringLiteral("home_state_changed");
            payload.insert(QStringLiteral("unclassifiedFiles"), QString::number(value.unclassifiedFiles));
            payload.insert(QStringLiteral("staleFiles"), QString::number(value.staleFiles));
            payload.insert(QStringLiteral("largeFiles"), QString::number(value.largeFiles));
            payload.insert(QStringLiteral("totalBytes"), QString::number(value.totalBytes));
        }
    }, event);

    if (eventType.isEmpty() ||
        (!updateSql.isEmpty() && previousStatus.isEmpty() && !fixedPreviousStates)) {
        database_.rollback();
        return Result<void>::failure({"history.transition_invalid", "This status transition is not allowed."});
    }
    if (!updateSql.isEmpty()) {
        QSqlQuery update(database_);
        update.prepare(updateSql);
        update.addBindValue(newStatus);
        update.addBindValue(stepId.isEmpty() ? planId : stepId);
        if (!stepId.isEmpty()) update.addBindValue(planId);
        if (!fixedPreviousStates) update.addBindValue(previousStatus);
        if (!update.exec() || update.numRowsAffected() != 1) {
            auto error = update.lastError().isValid()
                ? sqlFailure("history.transition_failed", update)
                : Result<void>::failure({"history.transition_conflict", "Stored status differs from expected status."});
            database_.rollback();
            return error;
        }
    }

    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO events(plan_id, step_id, occurred_at, event_type, payload_json) "
        "VALUES(?, ?, ?, ?, ?)"));
    insert.addBindValue(planId.isEmpty() ? QVariant() : QVariant(planId));
    insert.addBindValue(stepId.isEmpty() ? QVariant() : QVariant(stepId));
    insert.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    insert.addBindValue(eventType);
    insert.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    if (!insert.exec()) {
        auto error = sqlFailure("history.event_failed", insert);
        database_.rollback();
        return error;
    }
    if (!database_.commit()) {
        database_.rollback();
        return Result<void>::failure({"history.commit_failed", database_.lastError().text().toStdString()});
    }
    return Result<void>::success();
}

// 명시적 선택 감사와 단계·상위 상태 복원을 함께 저장하고 실패 시 모두 롤백하는 함수.
Result<void> SqliteHistoryRepository::beginRecovery(const RecoveryRestart& restart) {
    const auto& request = restart.request;
    if ((request.action != RecoveryAction::Resume && request.action != RecoveryAction::Rollback) ||
        !PlanSignature::isSha256Hex(request.planHash) ||
        (restart.expectedStatus != PlanStatus::Executing && restart.expectedStatus != PlanStatus::RollingBack && restart.expectedStatus != PlanStatus::NeedsRecovery) ||
        (request.action == RecoveryAction::Resume && restart.expectedStatus == PlanStatus::RollingBack))
        return Result<void>::failure({"history.recovery_invalid", "A current interrupted approval and explicit recovery choice are required."});
    const auto loaded = loadPlan(request.planId);
    if (!loaded) return Result<void>::failure(loaded.error());
    const auto& current = loaded.value();
    const auto digest = QCryptographicHash::hash(QByteArray::fromStdString(PlanSignature::approvalPayload(current)), QCryptographicHash::Sha256).toHex().toStdString();
    if (current.planHash != request.planHash || digest != request.planHash || current.status != restart.expectedStatus || current.steps.size() != restart.steps.size())
        return Result<void>::failure({"history.recovery_conflict", "Recovery snapshot changed before recording the choice."});
    if (restart.expectedStatus == PlanStatus::NeedsRecovery) {
        const auto origin = interruptedStatus(request.planId);
        if (!origin || (origin.value() != PlanStatus::Executing && origin.value() != PlanStatus::RollingBack) ||
            (request.action == RecoveryAction::Resume && origin.value() != PlanStatus::Executing))
            return Result<void>::failure({"history.recovery_direction", "Forward resume cannot replace an interrupted undo."});
    }
    std::set<StepId> ids;
    for (size_t i = 0; i < restart.steps.size(); ++i) {
        const auto& step = restart.steps[i];
        if (step.stepId != current.steps[i].id || step.expected != current.steps[i].status || !ids.insert(step.stepId).second ||
            (step.restored != StepStatus::Planned && step.restored != StepStatus::Applied && step.restored != StepStatus::RolledBack && step.restored != StepStatus::Skipped && step.restored != StepStatus::IntentRecorded) ||
            (step.restored == StepStatus::IntentRecorded && (request.action != RecoveryAction::Resume || !current.steps[i].movement ||
                current.steps[i].movement->strategy != MoveStrategy::VerifiedCopy ||
                (step.expected != StepStatus::IntentRecorded && step.expected != StepStatus::NeedsRecovery))) ||
            ((step.expected == StepStatus::Skipped) != (step.restored == StepStatus::Skipped)) ||
            (request.action == RecoveryAction::Resume && step.restored == StepStatus::RolledBack))
            return Result<void>::failure({"history.recovery_steps_invalid", "Every original step must appear once in order without changing skip decisions."});
    }
    if (!database_.transaction()) return Result<void>::failure({"history.transaction_failed", database_.lastError().text().toStdString()});
    // 실패 시 선택·사건·단계·상위 상태를 모두 되돌리는 함수.
    const auto fail = [&](const QSqlQuery& query) {
        const auto error = query.lastError().isValid() ? sqlFailure("history.recovery_write_failed", query) :
            Result<void>::failure({"history.recovery_conflict", "A compared recovery state changed."});
        database_.rollback(); return error;
    };
    const auto target = request.action == RecoveryAction::Resume ? PlanStatus::Executing : PlanStatus::RollingBack;
    QSqlQuery plan(database_);
    plan.prepare("UPDATE plans SET status=? WHERE id=? AND plan_hash=? AND status=? AND approved_at IS NOT NULL");
    plan.addBindValue(planStatus(target)); plan.addBindValue(QString::fromStdString(request.planId));
    plan.addBindValue(QString::fromStdString(request.planHash)); plan.addBindValue(planStatus(restart.expectedStatus));
    if (!plan.exec() || plan.numRowsAffected() != 1) return fail(plan);
    for (const auto& step : restart.steps) {
        QSqlQuery update(database_);
        update.prepare("UPDATE steps SET status=? WHERE id=? AND plan_id=? AND status=?");
        update.addBindValue(stepStatus(step.restored)); update.addBindValue(QString::fromStdString(step.stepId));
        update.addBindValue(QString::fromStdString(request.planId)); update.addBindValue(stepStatus(step.expected));
        if (!update.exec() || update.numRowsAffected() != 1) return fail(update);
    }
    QJsonArray states;
    for (const auto& step : restart.steps) states.append(QJsonObject{{"stepId", QString::fromStdString(step.stepId)},
        {"before", stepStatus(step.expected)}, {"after", stepStatus(step.restored)}});
    const QJsonObject choice{{"action", request.action == RecoveryAction::Resume ? "resume" : "rollback"},
        {"planHash", QString::fromStdString(request.planHash)}, {"beforeStatus", planStatus(restart.expectedStatus)},
        {"afterStatus", planStatus(target)}, {"steps", states}};
    // 동일한 트랜잭션 안에 사용자 선택과 중단 방향 추적 사건을 기록하는 함수.
    const auto event = [&](const QString& type, const QJsonObject& payload) -> Result<void> {
        QSqlQuery insert(database_);
        insert.prepare("INSERT INTO events(plan_id,occurred_at,event_type,payload_json) VALUES(?,?,?,?)");
        insert.addBindValue(QString::fromStdString(request.planId)); insert.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        insert.addBindValue(type); insert.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        if (!insert.exec()) return fail(insert);
        return Result<void>::success();
    };
    const auto audit = event("recovery_choice_recorded", choice);
    if (!audit) return audit;
    const auto direction = event("plan_status_changed", {{"status", planStatus(target)}});
    if (!direction) return direction;
    if (!database_.commit()) {
        database_.rollback(); return Result<void>::failure({"history.commit_failed", database_.lastError().text().toStdString()});
    }
    return Result<void>::success();
}

// 아직 완료되지 않은 저장 계획을 읽어오는 함수.
Result<std::vector<OperationPlan>> SqliteHistoryRepository::incompletePlans() const {
    return readPlans(QStringLiteral(
        "status NOT IN ('completed', 'rolled_back', 'cancelled')"));
}

// 완료된 계획까지 포함해 ID 하나로 읽는 함수.
Result<OperationPlan> SqliteHistoryRepository::loadPlan(const PlanId& planId) const {
    if (planId.empty()) {
        return Result<OperationPlan>::failure({"history.plan_id_empty", "Plan ID is required."});
    }
    auto plans = readPlans(QStringLiteral("id = ?"), QString::fromStdString(planId));
    if (!plans) return Result<OperationPlan>::failure(plans.error());
    if (plans.value().size() != 1) {
        return Result<OperationPlan>::failure({"history.plan_missing", "Plan was not found."});
    }
    return Result<OperationPlan>::success(std::move(plans.value().front()));
}

// 최근 실행 작업의 상태와 파일 수를 최신순으로 읽는 함수.
Result<std::vector<PlanHistoryEntry>> SqliteHistoryRepository::recentPlans(int limit) const {
    if (limit < 1 || limit > 100) {
        return Result<std::vector<PlanHistoryEntry>>::failure({
            "history.limit_invalid", "History limit must be between 1 and 100.",
        });
    }
    auto opened = ensureOpen();
    if (!opened) return Result<std::vector<PlanHistoryEntry>>::failure(opened.error());
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT p.id, COALESCE((SELECT MAX(e.occurred_at) FROM events e WHERE e.plan_id = p.id), "
        "p.created_at), p.status, "
        "(SELECT COUNT(*) FROM steps s WHERE s.plan_id = p.id AND s.status != 'skipped'), "
        "(SELECT COUNT(*) FROM steps s WHERE s.plan_id = p.id AND s.status = 'applied') "
        "FROM plans p WHERE p.status IN "
        "('executing', 'completed', 'rolling_back', 'rolled_back', 'needs_recovery', 'failed') "
        "ORDER BY 2 DESC, p.id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        return Result<std::vector<PlanHistoryEntry>>::failure({
            "history.recent_select_failed", query.lastError().text().toStdString(),
        });
    }
    std::vector<PlanHistoryEntry> result;
    while (query.next()) {
        const auto status = parsePlanStatus(query.value(2).toString());
        if (!status) {
            return Result<std::vector<PlanHistoryEntry>>::failure({
                "history.corrupt_plan", "A saved plan has an invalid status.",
            });
        }
        result.push_back({query.value(0).toString().toStdString(),
                          query.value(1).toString().toStdString(), *status,
                          query.value(3).toInt(), query.value(4).toInt()});
    }
    return Result<std::vector<PlanHistoryEntry>>::success(std::move(result));
}

// 확인 필요 상태 전의 실행·되돌리기 상태를 이벤트 기록에서 찾는 함수.
Result<PlanStatus> SqliteHistoryRepository::interruptedStatus(const PlanId& planId) const {
    auto opened = ensureOpen();
    if (!opened) return Result<PlanStatus>::failure(opened.error());
    QSqlQuery events(database_);
    events.prepare(QStringLiteral(
        "SELECT payload_json FROM events WHERE plan_id = ? AND event_type = 'plan_status_changed' "
        "ORDER BY id DESC"));
    events.addBindValue(QString::fromStdString(planId));
    if (!events.exec()) {
        return Result<PlanStatus>::failure({
            "history.select_events_failed", events.lastError().text().toStdString(),
        });
    }
    while (events.next()) {
        const auto payload = QJsonDocument::fromJson(events.value(0).toString().toUtf8());
        if (!payload.isObject()) continue;
        const auto state = payload.object().value(QStringLiteral("status")).toString();
        if (state == QStringLiteral("executing")) {
            return Result<PlanStatus>::success(PlanStatus::Executing);
        }
        if (state == QStringLiteral("rolling_back")) {
            return Result<PlanStatus>::success(PlanStatus::RollingBack);
        }
    }
    return Result<PlanStatus>::failure({
        "history.recovery_origin_missing", "Cannot determine the interrupted operation.",
    });
}

// 주어진 조건에 맞는 계획과 단계들을 DB에서 복원하는 함수.
Result<std::vector<OperationPlan>> SqliteHistoryRepository::readPlans(
    const QString& whereClause, const QString& planId) const {
    auto opened = ensureOpen();
    if (!opened) return Result<std::vector<OperationPlan>>::failure(opened.error());
    QSqlQuery plans(database_);
    plans.prepare(QStringLiteral(
        "SELECT id, rule_set_version, settings_revision, rules_revision, plan_hash, "
        "status, warnings_json, format_version FROM plans WHERE ") + whereClause +
        QStringLiteral(" ORDER BY created_at, id"));
    if (!planId.isEmpty()) plans.addBindValue(planId);
    if (!plans.exec()) {
        return Result<std::vector<OperationPlan>>::failure({
            "history.select_failed", plans.lastError().text().toStdString(),
        });
    }
    std::vector<OperationPlan> result;
    while (plans.next()) {
        OperationPlan plan;
        plan.id = plans.value(0).toString().toStdString();
        plan.ruleSetVersion = plans.value(1).toString().toStdString();
        plan.settingsRevision = plans.value(2).toString().toStdString();
        plan.rulesRevision = plans.value(3).toString().toStdString();
        plan.planHash = plans.value(4).toString().toStdString();
        const auto status = parsePlanStatus(plans.value(5).toString());
        const auto warnings = parseWarnings(plans.value(6).toString());
        if (!status || !warnings) {
            return Result<std::vector<OperationPlan>>::failure({
                "history.corrupt_plan", "A saved plan has invalid status or warnings.",
            });
        }
        plan.status = *status;
        plan.warnings = *warnings;
        bool versionOk = false;
        plan.formatVersion = plans.value(7).toInt(&versionOk);
        if (!versionOk || plans.value(7).toDouble() != plan.formatVersion ||
            (plan.formatVersion != 1 && plan.formatVersion != 2))
            return Result<std::vector<OperationPlan>>::failure({"plan.format_unsupported", "Unsupported stored approval version."});

        QSqlQuery steps(database_);
        steps.prepare(QStringLiteral(
            "SELECT id, source_path, destination_path, fingerprint_json, matched_rule_id, "
            "reason, collision_policy, status, movement_json FROM steps WHERE plan_id = ? ORDER BY sequence_no"));
        steps.addBindValue(QString::fromStdString(plan.id));
        if (!steps.exec()) {
            return Result<std::vector<OperationPlan>>::failure({
                "history.select_steps_failed", steps.lastError().text().toStdString(),
            });
        }
        while (steps.next()) {
            MoveStep step;
            step.id = steps.value(0).toString().toStdString();
            step.source = std::filesystem::path(steps.value(1).toString().toStdWString());
            step.destination = std::filesystem::path(steps.value(2).toString().toStdWString());
            const auto fingerprint = parseFingerprint(steps.value(3).toString());
            const auto collision = parseCollisionPolicy(steps.value(6).toString());
            const auto stepState = parseStepStatus(steps.value(7).toString());
            if (!fingerprint || !collision || !stepState) {
                return Result<std::vector<OperationPlan>>::failure({
                    "history.corrupt_step", "A saved step has invalid metadata.",
                });
            }
            step.fingerprint = *fingerprint;
            step.matchedRuleId = steps.value(4).toString().toStdString();
            step.reason = steps.value(5).toString().toStdString();
            step.collisionPolicy = *collision;
            step.status = *stepState;
            if (!steps.value(8).isNull()) {
                step.movement = parseMovement(steps.value(8).toString());
                if (!step.movement) return Result<std::vector<OperationPlan>>::failure({"history.corrupt_contract", "Invalid stored movement contract."});
            }
            plan.steps.push_back(std::move(step));
        }
        const auto format = PlanSignature::validateFormat(plan);
        if (!format) return Result<std::vector<OperationPlan>>::failure(format.error());
        result.push_back(std::move(plan));
    }
    return Result<std::vector<OperationPlan>>::success(std::move(result));
}

} // namespace nookbit
