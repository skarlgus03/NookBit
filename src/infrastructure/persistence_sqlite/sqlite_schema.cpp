#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include <array>

#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif

namespace nookbit {
namespace {

constexpr std::array migrationFiles{
    "001_initial.sql", "002_transfer_journal.sql", "003_plan_contract.sql",
    "004_transfer_recovery.sql", "005_staging_cleanup.sql", "006_recovery_removal.sql",
    "007_copy_resume.sql", "008_staging_commit.sql", "009_removal_proof.sql"
};

// SQL 문장 앞에 붙은 공백과 주석을 건너뛰는 함수.
QString migrationSql(const QByteArray& statement) {
    static const QRegularExpression leadingTrivia(
        QStringLiteral(R"(\A(?:\s+|--[^\r\n]*|/\*[\s\S]*?\*/)*)"));
    const auto sql = QString::fromUtf8(statement);
    return sql.mid(leadingTrivia.match(sql).capturedEnd());
}

// SQLite가 완성됐다고 판단한 문장 하나를 현재 Qt 연결에서 실행하는 함수.
Result<void> executeMigrationStatement(QSqlDatabase& database, const QByteArray& statement) {
    const auto sql = migrationSql(statement);
    if (sql.isEmpty() || sql == QStringLiteral(";")) return Result<void>::success();
    static const QRegularExpression firstKeyword(QStringLiteral(R"(\A[a-z]+)"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const auto keyword = firstKeyword.match(sql).captured().toUpper();
    // 연결 설정은 트랜잭션 밖에서 적용하며, 스크립트는 트랜잭션을 소유하지 않는다.
    if (keyword == QStringLiteral("PRAGMA")) return Result<void>::success();
    if (keyword == QStringLiteral("BEGIN") || keyword == QStringLiteral("COMMIT") ||
        keyword == QStringLiteral("END") || keyword == QStringLiteral("ROLLBACK") ||
        keyword == QStringLiteral("SAVEPOINT") || keyword == QStringLiteral("RELEASE"))
        return Result<void>::failure({"history.migration_failed", "Migration must not control the transaction."});
    QSqlQuery query(database);
    if (!query.exec(sql))
        return Result<void>::failure({"history.migration_failed", query.lastError().text().toStdString()});
    return Result<void>::success();
}

// 문자열·주석·트리거 내부의 세미콜론을 포함한 스크립트를 문장별로 적용하는 함수.
Result<void> executeMigrationScript(QSqlDatabase& database, QByteArray script) {
    if (script.startsWith("\xef\xbb\xbf")) script.remove(0, 3);
    if (script.contains('\0'))
        return Result<void>::failure({"history.migration_failed", "Migration contains an embedded NUL."});
    qsizetype start = 0;
    for (auto end = script.indexOf(';'); end >= 0; end = script.indexOf(';', end + 1)) {
        const auto statement = script.mid(start, end - start + 1);
        const int complete = sqlite3_complete(statement.constData());
        if (complete == SQLITE_NOMEM)
            return Result<void>::failure({"history.migration_failed", "SQLite could not inspect the migration statement."});
        if (complete != 1) continue;
        const auto executed = executeMigrationStatement(database, statement);
        if (!executed) return executed;
        start = end + 1;
    }
    return executeMigrationStatement(database, script.mid(start));
}

} // namespace

// 기존 기록을 보존하며 지원하는 SQLite 버전까지 순서대로 이행하는 함수.
Result<void> migrateHistorySchema(QSqlDatabase& database, const QString& initialMigrationPath) {
    constexpr int latestVersion = static_cast<int>(migrationFiles.size());
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
        const auto path = version == 1 ? initialMigrationPath : QDir(QFileInfo(initialMigrationPath).path())
            .filePath(QString::fromLatin1(migrationFiles[static_cast<std::size_t>(version - 1)]));
        QFile migration(path);
        if (!migration.open(QIODevice::ReadOnly))
            return Result<void>::failure({"history.migration_missing",
                QStringLiteral("Cannot read a required SQLite migration: %1 (%2)")
                    .arg(path, migration.errorString()).toStdString()});
        if (!database.transaction())
            return Result<void>::failure({"history.transaction_failed", database.lastError().text().toStdString()});
        const auto script = migration.readAll();
        if (migration.error() != QFileDevice::NoError) {
            database.rollback();
            return Result<void>::failure({"history.migration_missing", migration.errorString().toStdString()});
        }
        const auto executed = executeMigrationScript(database, script);
        if (!executed) {
            database.rollback();
            return executed;
        }
        QSqlQuery marker(database);
        if (!marker.exec(QStringLiteral("SELECT max(version) FROM schema_migrations")) ||
            !marker.next() || marker.value(0).toInt() != version) {
            database.rollback();
            return Result<void>::failure({"history.migration_failed", "Migration did not record its expected schema version."});
        }
        marker.finish();
        if (!database.commit()) {
            const auto message = database.lastError().text().toStdString();
            database.rollback();
            return Result<void>::failure({"history.commit_failed", message});
        }
    }
    return Result<void>::success();
}

} // namespace nookbit
