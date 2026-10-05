#pragma once

#include <string>

namespace pinax::db {

class Connection;

// A nested unit of work inside a Transaction. Destroyed without release(), it
// rolls back to where it began and the enclosing transaction carries on. The
// importer uses one per row, so a row that fails leaves nothing behind while
// the rows around it still commit together.
class Savepoint {
public:
    Savepoint(Connection& connection, std::string name);
    ~Savepoint();

    Savepoint(const Savepoint&) = delete;
    Savepoint& operator=(const Savepoint&) = delete;

    void release();

private:
    Connection& connection_;
    std::string name_;
    bool open_ = true;
};

} // namespace pinax::db
