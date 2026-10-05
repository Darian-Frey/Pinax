#include "db/savepoint.h"

#include "db/connection.h"
#include "db/db_error.h"

#include <utility>

namespace pinax::db {

Savepoint::Savepoint(Connection& connection, std::string name)
    : connection_(connection)
    , name_(std::move(name))
{
    connection_.exec("SAVEPOINT " + name_);
}

Savepoint::~Savepoint()
{
    if (!open_)
        return;
    try {
        // ROLLBACK TO undoes the work but leaves the savepoint on the stack;
        // RELEASE removes it.
        connection_.exec("ROLLBACK TO " + name_);
        connection_.exec("RELEASE " + name_);
    } catch (const DbError&) {
        // The enclosing transaction has already gone; nothing left to undo.
    }
}

void Savepoint::release()
{
    connection_.exec("RELEASE " + name_);
    open_ = false;
}

} // namespace pinax::db
