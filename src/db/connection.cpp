#include "db/connection.h"

#include "db/db_error.h"
#include "db/statement.h"

#include <sqlite3.h>

namespace pinax::db {

Connection::Connection(const std::string& path)
{
    const int rc = sqlite3_open_v2(path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("cannot open " + path + ": " + message, rc);
    }
    sqlite3_extended_result_codes(db_, 1);

    try {
        // Per-connection and never persisted: without it every REFERENCES
        // clause is advisory and cascades do not fire (AV-004).
        exec("PRAGMA foreign_keys = ON");
        if (!foreignKeysEnabled())
            throw DbError("foreign key enforcement could not be enabled on " + path, SQLITE_ERROR);

        // Persisted in the file once set. An in-memory database stays in
        // 'memory' mode, which is fine.
        exec("PRAGMA journal_mode = WAL");
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

Connection::~Connection()
{
    // Every Statement must already be finalised; sqlite3_close refuses
    // otherwise, and leaking the handle is the lesser fault in a destructor.
    sqlite3_close(db_);
}

void Connection::exec(std::string_view sql)
{
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errstr(rc);
        sqlite3_free(error);
        throw DbError(message, sqlite3_extended_errcode(db_));
    }
}

bool Connection::foreignKeysEnabled()
{
    Statement query(*this, "PRAGMA foreign_keys");
    return query.step() && query.columnInt(0) == 1;
}

} // namespace pinax::db
