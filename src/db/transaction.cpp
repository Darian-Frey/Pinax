#include "db/transaction.h"

#include "db/connection.h"
#include "db/db_error.h"

namespace pinax::db {

Transaction::Transaction(Connection& connection)
    : connection_(connection)
{
    connection_.exec("BEGIN IMMEDIATE");
}

Transaction::~Transaction()
{
    if (!open_)
        return;
    try {
        connection_.exec("ROLLBACK");
    } catch (const DbError&) {
        // SQLite may already have rolled back after the error that brought us
        // here; there is nothing further to undo.
    }
}

void Transaction::commit()
{
    connection_.exec("COMMIT");
    open_ = false;
}

} // namespace pinax::db
