#pragma once

#include <stdexcept>
#include <string>

namespace pinax::db {

// A failure reported by SQLite. `code()` is the extended result code, so a
// caller can tell a constraint violation (SQLITE_CONSTRAINT_*) from I/O
// trouble. Callers for whom a failure is expected — the importer's per-row
// reporting — catch this and turn it into a value (ARCHITECTURE.md §4).
class DbError : public std::runtime_error {
public:
    DbError(const std::string& message, int code)
        : std::runtime_error(message)
        , code_(code)
    {
    }

    int code() const { return code_; }

    // A CHECK, UNIQUE, NOT NULL or foreign-key violation: the data was wrong,
    // not the database.
    bool isConstraintViolation() const { return (code_ & 0xff) == 19; } // SQLITE_CONSTRAINT

private:
    int code_;
};

} // namespace pinax::db
