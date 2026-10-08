#pragma once

namespace pinax::db {

class Connection;

// The schema version this build writes. Must equal the highest version
// db/schema.sql inserts into `schema_version` and the last migration step's;
// migrate() checks the result and the tests hold them together.
inline constexpr int latestSchemaVersion = 6;

// The database's schema version, or 0 for a database with no schema yet.
int schemaVersion(Connection& connection);

// Brings the database up to latestSchemaVersion. An empty database receives
// db/schema.sql whole; an older one steps forward one version at a time. Each
// step is a single transaction, so a failure leaves the previous version
// intact. Throws DbError if the database is newer than this build, or if a
// step fails. Returns the resulting version.
int migrate(Connection& connection);

} // namespace pinax::db
