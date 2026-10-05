#include "db/migrations.h"

#include "db/connection.h"
#include "db/db_error.h"
#include "db/schema_sql.h"
#include "db/statement.h"
#include "db/transaction.h"

#include <sqlite3.h>

#include <span>
#include <string>
#include <string_view>

namespace pinax::db {

namespace {

struct Step {
    int version;          // the version this step produces
    std::string_view sql; // must also insert its row into schema_version
};

// Steps from each version to the next. Empty while there is only version 1:
// db/schema.sql is always the latest full schema, and these exist only to
// carry an older file forward.
constexpr std::span<const Step> steps{};

} // namespace

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
        for (const Step& step : steps) {
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
