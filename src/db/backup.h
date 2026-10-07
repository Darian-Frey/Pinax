#pragma once

#include <cstdint>
#include <string>

namespace pinax::db {

class Connection;

struct BackupReport {
    std::int64_t books = 0; // in the copy, equal to the live catalogue's
    std::string path;       // where it was written
};

// A consistent copy of the live database at `path`, taken while it stays
// open (F-020, D-002, SPEC.md §5.4). Never a file copy, which in WAL mode
// can miss what the log holds (AV-003): `VACUUM INTO` a temporary file beside
// `path`, which is then opened read-only and checked — integrity, the schema
// version, the same number of books — and only then renamed over `path`,
// replacing any file there. The copy is a single file in rollback mode. On
// any failure the temporary file is removed, `path` is untouched, and
// DbError is thrown with the reason.
BackupReport backupTo(Connection& live, const std::string& path);

} // namespace pinax::db
