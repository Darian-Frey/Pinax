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

// What a file is, looked at read-only before anything opens it for writing
// (F-026): a Pinax catalogue at some schema version, or not.
struct CatalogueFile {
    bool exists = false;
    bool empty = false;      // no file, or a file of no bytes: a new catalogue may go there
    bool catalogue = false;  // an SQLite database with Pinax's schema_version table
    int version = 0;         // its schema version, if a catalogue
    std::int64_t books = 0;  // if a catalogue
    std::string problem;     // why it is not one, in the owner's terms
};
CatalogueFile inspect(const std::string& path);

// Replaces the catalogue at `target` — which must not be open — with the one
// at `source`, a backup (F-027): `source` is copied with VACUUM INTO to a
// partial file beside `target`, checked as backupTo checks, and only then
// renamed over `target`, whose -wal and -shm files go first. On any failure
// `target` is untouched and DbError is thrown.
BackupReport restoreFrom(const std::string& source, const std::string& target);

} // namespace pinax::db
