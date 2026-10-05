#include "db/migrations.h"

#include "db/connection.h"
#include "db/db_error.h"
#include "db/schema_sql.h"
#include "db/statement.h"
#include "db/transaction.h"

#include <sqlite3.h>

#include <string>
#include <string_view>

namespace pinax::db {

int schemaVersion(Connection& connection)
{
    Statement exists(connection,
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_version'");
    if (!exists.step())
        return 0;

    Statement version(connection, "SELECT MAX(version) FROM schema_version");
    version.step();
    return static_cast<int>(version.columnInt(0));
}

int migrate(Connection& connection)
{
    int version = schemaVersion(connection);

    if (version > latestSchemaVersion) {
        throw DbError("database schema version " + std::to_string(version)
                + " is newer than this build supports ("
                + std::to_string(latestSchemaVersion) + ")",
            SQLITE_ERROR);
    }

    if (version == 0) {
        Transaction transaction(connection);
        connection.exec(schemaSql);
        transaction.commit();
    } else {
        for (const MigrationStep& step : migrationSteps) {
            if (step.version <= version)
                continue;
            Transaction transaction(connection);
            connection.exec(step.sql);
            transaction.commit();
        }
    }

    version = schemaVersion(connection);
    if (version != latestSchemaVersion) {
        throw DbError("migration ended at schema version " + std::to_string(version)
                + ", expected " + std::to_string(latestSchemaVersion),
            SQLITE_ERROR);
    }
    return version;
}

} // namespace pinax::db
