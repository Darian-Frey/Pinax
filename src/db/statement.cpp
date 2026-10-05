#include "db/statement.h"

#include "db/connection.h"
#include "db/db_error.h"

#include <sqlite3.h>

namespace pinax::db {

namespace {

[[noreturn]] void fail(Connection& connection, const std::string& context)
{
    sqlite3* db = connection.handle();
    throw DbError(context + ": " + sqlite3_errmsg(db), sqlite3_extended_errcode(db));
}

} // namespace

Statement::Statement(Connection& connection, std::string_view sql)
    : connection_(connection)
{
    const int rc = sqlite3_prepare_v2(connection_.handle(), sql.data(),
        static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK)
        fail(connection_, "cannot prepare '" + std::string(sql) + "'");
}

Statement::~Statement()
{
    sqlite3_finalize(stmt_);
}

int Statement::parameterIndex(const char* name) const
{
    const int index = sqlite3_bind_parameter_index(stmt_, name);
    if (index == 0)
        throw DbError(std::string("no parameter named ") + name, SQLITE_RANGE);
    return index;
}

void Statement::bind(const char* name, std::int64_t value)
{
    if (sqlite3_bind_int64(stmt_, parameterIndex(name), value) != SQLITE_OK)
        fail(connection_, std::string("cannot bind ") + name);
}

void Statement::bind(const char* name, std::string_view value)
{
    if (sqlite3_bind_text(stmt_, parameterIndex(name), value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT)
        != SQLITE_OK)
        fail(connection_, std::string("cannot bind ") + name);
}

void Statement::bindNull(const char* name)
{
    if (sqlite3_bind_null(stmt_, parameterIndex(name)) != SQLITE_OK)
        fail(connection_, std::string("cannot bind ") + name);
}

bool Statement::step()
{
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    fail(connection_, std::string("cannot execute '") + sqlite3_sql(stmt_) + "'");
}

bool Statement::columnIsNull(int index) const
{
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

std::int64_t Statement::columnInt(int index) const
{
    return sqlite3_column_int64(stmt_, index);
}

double Statement::columnDouble(int index) const
{
    return sqlite3_column_double(stmt_, index);
}

std::string Statement::columnText(int index) const
{
    const auto* text = sqlite3_column_text(stmt_, index);
    if (!text)
        return {};
    return std::string(reinterpret_cast<const char*>(text),
        static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index)));
}

std::optional<std::int64_t> Statement::columnOptionalInt(int index) const
{
    if (columnIsNull(index))
        return std::nullopt;
    return columnInt(index);
}

std::optional<double> Statement::columnOptionalDouble(int index) const
{
    if (columnIsNull(index))
        return std::nullopt;
    return columnDouble(index);
}

std::optional<std::string> Statement::columnOptionalText(int index) const
{
    if (columnIsNull(index))
        return std::nullopt;
    return columnText(index);
}

} // namespace pinax::db
