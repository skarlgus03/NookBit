#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

namespace nookbit {

// 기존 기록을 보존하며 지원하는 SQLite 버전까지 순서대로 이행하는 함수.
Result<void> migrateHistorySchema(QSqlDatabase& database, const QString& initialMigrationPath) {
    constexpr int latestVersion = 6;
    int currentVersion = 0;
    {
        QSqlQuery table(database);
        if (!table.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_migrations'")))
            return Result<void>::failure({"history.migration_failed", table.lastError().text().toStdString()});
        if (table.next()) {
            QSqlQuery versions(database);
            if (!versions.exec(QStringLiteral("SELECT version FROM schema_migrations ORDER BY version")))
                return Result<void>::failure({"history.migration_failed", versions.lastError().text().toStdString()});
            while (versions.next()) {
                const auto version = versions.value(0).toInt();
                if (version > latestVersion) return Result<void>::failure({"history.schema_too_new", "This database needs a newer NookBit version."});
                if (version != currentVersion + 1) return Result<void>::failure({"history.schema_invalid", "Migration versions are not contiguous."});
                currentVersion = version;
            }
        }
    }
    // 지원 여부 확인 뒤에만 연결 설정과 마이그레이션을 변경한다.
    for (const auto& pragma : {"PRAGMA foreign_keys = ON", "PRAGMA journal_mode = WAL", "PRAGMA synchronous = FULL"}) {
        QSqlQuery query(database);
        if (!query.exec(QString::fromLatin1(pragma)))
            return Result<void>::failure({"history.migration_failed", query.lastError().text().toStdString()});
    }
    for (int version = currentVersion + 1; version <= latestVersion; ++version) {
        const auto path = version == 1 ? initialMigrationPath :
            QDir(QFileInfo(initialMigrationPath).path()).filePath(version == 2 ?
                QStringLiteral("002_transfer_journal.sql") : version == 3 ?
                QStringLiteral("003_plan_contract.sql") : version == 4 ?
                QStringLiteral("004_transfer_recovery.sql") : version == 5 ?
                QStringLiteral("005_staging_cleanup.sql") : QStringLiteral("006_recovery_removal.sql"));
        QFile migration(path);
        if (!migration.open(QIODevice::ReadOnly))
            return Result<void>::failure({"history.migration_missing", "Cannot read a required SQLite migration."});
        if (!database.transaction())
            return Result<void>::failure({"history.transaction_failed", database.lastError().text().toStdString()});
        const auto statements = QString::fromUtf8(migration.readAll()).split(';', Qt::SkipEmptyParts);
        for (const auto& statement : statements) {
            const auto sql = statement.trimmed();
            if (sql.isEmpty() || sql.startsWith(QStringLiteral("PRAGMA"), Qt::CaseInsensitive)) continue;
            QSqlQuery query(database);
            if (!query.exec(sql)) {
                const auto message = query.lastError().text().toStdString();
                database.rollback();
                return Result<void>::failure({"history.migration_failed", message});
            }
        }
        QSqlQuery marker(database);
        if (!marker.exec(QStringLiteral("SELECT max(version) FROM schema_migrations")) ||
            !marker.next() || marker.value(0).toInt() != version) {
            database.rollback();
            return Result<void>::failure({"history.migration_failed", "Migration did not record its expected schema version."});
        }
        marker.finish();
        if (!database.commit()) {
            database.rollback();
            return Result<void>::failure({"history.commit_failed", database.lastError().text().toStdString()});
        }
    }
    return Result<void>::success();
}

} // namespace nookbit
