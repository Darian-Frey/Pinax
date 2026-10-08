#pragma once

#include <cstdint>
#include <ostream>
#include <string>

namespace pinax::db {

class Connection;

// The catalogue as plain-text SQL that recreates it on an empty database
// (F-021, SPEC.md §5.3), in the manner of `sqlite3 .dump`: within one
// transaction and with foreign keys off, each table's CREATE then its rows,
// ordered by key, one INSERT a line; then indexes, views and triggers, the
// triggers last so no row restored sets one off. Deterministic — no time or
// other run-dependent detail — so an unchanged catalogue dumps to the same
// bytes and a diff shows only what changed. Text keeps its quotes, line
// breaks and accents; reals are written to round-trip exactly.
void writeDump(Connection& connection, std::ostream& out);

struct DumpReport {
    std::int64_t books = 0;
    std::int64_t rows = 0; // in every table
    std::string path;
};

// Writes the dump to a partial file beside `path`, restores it into an
// empty in-memory database, checks every table's row count matches and that
// the restored catalogue dumps to the very same text, then renames it over
// `path`. On any failure the partial
// file is removed, `path` is untouched, and DbError is thrown.
DumpReport dumpTo(Connection& connection, const std::string& path);

// A new catalogue at `target` built from the dump at `dumpPath` (F-027):
// run into a partial file beside `target`, checked — integrity, a schema
// version this Pinax knows, nothing broken by a foreign key — and then
// renamed into place. Refuses a `target` that already exists. On any
// failure nothing is left at `target`, and DbError is thrown.
DumpReport restoreDump(const std::string& dumpPath, const std::string& target);

} // namespace pinax::db
