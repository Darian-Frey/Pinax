#pragma once

#include <string>
#include <string_view>

struct sqlite3;

namespace pinax::db {

// Owns one SQLite connection. Opening it enables foreign-key enforcement and
// verifies that it took (ARCHITECTURE.md invariant 6, AV-004), and puts a
// file database into WAL mode. Throws DbError if either fails.
class Connection {
public:
    // `path` is a file, created if absent, or ":memory:".
    explicit Connection(const std::string& path);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Runs one or more statements that return no rows the caller wants.
    void exec(std::string_view sql);

    bool foreignKeysEnabled();

    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

} // namespace pinax::db
