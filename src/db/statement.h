#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3_stmt;

namespace pinax::db {

class Connection;

// A prepared statement. Parameters are bound by name (":title"), columns read
// by zero-based index. Throws DbError on any SQLite failure.
class Statement {
public:
    Statement(Connection& connection, std::string_view sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(const char* name, std::int64_t value);
    void bind(const char* name, std::string_view value);
    void bindNull(const char* name);

    template <typename T>
    void bind(const char* name, const std::optional<T>& value)
    {
        if (value)
            bind(name, *value);
        else
            bindNull(name);
    }

    // Advances to the next row. True while a row is available; false once the
    // statement has run to completion.
    bool step();

    bool columnIsNull(int index) const;
    std::int64_t columnInt(int index) const;
    double columnDouble(int index) const;
    std::string columnText(int index) const;
    std::optional<std::int64_t> columnOptionalInt(int index) const;
    std::optional<double> columnOptionalDouble(int index) const;
    std::optional<std::string> columnOptionalText(int index) const;

private:
    int parameterIndex(const char* name) const;

    Connection& connection_;
    sqlite3_stmt* stmt_ = nullptr;
};

} // namespace pinax::db
