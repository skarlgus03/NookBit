#pragma once

#include <nookbit/domain/result.hpp>
#include <QSqlDatabase>
#include <QString>

namespace nookbit {

/// 기존 기록을 보존하며 지원하는 SQLite 버전까지 순서대로 이행하는 함수.
[[nodiscard]] Result<void> migrateHistorySchema(QSqlDatabase& database, const QString& initialMigrationPath);

} // namespace nookbit
