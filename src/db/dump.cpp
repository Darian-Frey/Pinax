#include "db/dump.h"

#include "db/connection.h"
#include "db/db_error.h"

#include <sqlite3.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace pinax::db {

namespace {

namespace fs = std::filesystem;

// A prepared statement that finalises itself.
class Query {
public:
    Query(sqlite3* db, const std::string& sql)
    {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement_, nullptr) != SQLITE_OK)
            throw DbError("cannot prepare '" + sql + "': " + sqlite3_errmsg(db), sqlite3_extended_errcode(db));
    }
    ~Query() { sqlite3_finalize(statement_); }
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    bool step() { return sqlite3_step(statement_) == SQLITE_ROW; }
    sqlite3_stmt* get() const { return statement_; }
    std::string text(int column) const
    {
        const auto* bytes = sqlite3_column_text(statement_, column);
        return bytes ? reinterpret_cast<const char*>(bytes) : std::string();
    }

private:
    sqlite3_stmt* statement_ = nullptr;
};

// A string literal. Control characters other than newline and tab — the
// carriage returns in some providers' text, say — are spelled out as
// char(n), as `sqlite3 .dump` does: a bare one would not survive editors,
// git or the sqlite3 shell, and the text would come back changed.
std::string quoted(const std::string& text)
{
    std::string out = "'";
    bool open = true;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 && c != '\n' && c != '\t') {
            out += (open ? "'||char(" : "||char(") + std::to_string(byte) + ")";
            open = false;
            continue;
        }
        if (!open) {
            out += "||'";
            open = true;
        }
        out += c == '\'' ? std::string("''") : std::string(1, c);
    }
    return open ? out + "'" : out;
}

// An SQL literal for one column of the current row.
std::string literal(sqlite3_stmt* row, int column)
{
    switch (sqlite3_column_type(row, column)) {
    case SQLITE_NULL:
        return "NULL";
    case SQLITE_INTEGER:
        return std::to_string(sqlite3_column_int64(row, column));
    case SQLITE_FLOAT: {
        const double value = sqlite3_column_double(row, column);
        char buffer[40];
        // As few digits as give the same double back; always a real.
        for (int digits = 15; digits <= 17; ++digits) {
            std::snprintf(buffer, sizeof buffer, "%.*g", digits, value);
            if (std::strtod(buffer, nullptr) == value)
                break;
        }
        std::string text = buffer;
        if (text.find_first_of(".eEn") == std::string::npos)
            text += ".0";
        return text;
    }
    case SQLITE_BLOB: {
        const auto* bytes = static_cast<const unsigned char*>(sqlite3_column_blob(row, column));
        const int size = sqlite3_column_bytes(row, column);
        static const char* hex = "0123456789ABCDEF";
        std::string out = "X'";
        for (int i = 0; i < size; ++i) {
            out += hex[bytes[i] >> 4];
            out += hex[bytes[i] & 0x0f];
        }
        return out + "'";
    }
    default: {
        const auto* bytes = sqlite3_column_text(row, column);
        return quoted(bytes ? reinterpret_cast<const char*>(bytes) : "");
    }
    }
}

struct SchemaObject {
    std::string type;
    std::string name;
    std::string sql;
};

std::vector<SchemaObject> schemaObjects(sqlite3* db)
{
    std::vector<SchemaObject> objects;
    Query query(db,
        "SELECT type, name, sql FROM sqlite_master"
        " WHERE sql IS NOT NULL AND name NOT LIKE 'sqlite_%' ORDER BY rowid");
    while (query.step())
        objects.push_back({query.text(0), query.text(1), query.text(2)});
    return objects;
}

// Every user table's row count, by name.
std::map<std::string, std::int64_t> rowCounts(sqlite3* db)
{
    std::map<std::string, std::int64_t> counts;
    for (const auto& object : schemaObjects(db)) {
        if (object.type != "table")
            continue;
        Query count(db, "SELECT COUNT(*) FROM \"" + object.name + "\"");
        count.step();
        counts[object.name] = sqlite3_column_int64(count.get(), 0);
    }
    return counts;
}

} // namespace

namespace {

void write(sqlite3* db, std::ostream& out)
{
    std::int64_t version = 0;
    {
        Query query(db, "SELECT MAX(version) FROM schema_version");
        if (query.step())
            version = sqlite3_column_int64(query.get(), 0);
    }
    out << "-- Pinax catalogue, schema version " << version << ".\n"
        << "-- Plain SQL that recreates it on an empty database (F-021):\n"
        << "--   sqlite3 restored.db < this-file.sql\n"
        << "PRAGMA foreign_keys=OFF;\n"
        << "BEGIN TRANSACTION;\n";

    const auto objects = schemaObjects(db);
    for (const auto& object : objects) {
        if (object.type != "table")
            continue;
        out << object.sql << ";\n";
        // Key order: rowid for ordinary tables, the declared key otherwise.
        Query rows(db, "SELECT * FROM \"" + object.name + "\" ORDER BY 1, 2");
        const int columns = sqlite3_column_count(rows.get());
        while (rows.step()) {
            out << "INSERT INTO " << object.name << " VALUES(";
            for (int column = 0; column < columns; ++column) {
                if (column > 0)
                    out << ',';
                out << literal(rows.get(), column);
            }
            out << ");\n";
        }
    }
    for (const char* type : {"index", "view", "trigger"}) {
        for (const auto& object : objects) {
            if (object.type == type)
                out << object.sql << ";\n";
        }
    }
    out << "COMMIT;\n";
}

} // namespace

void writeDump(Connection& connection, std::ostream& out)
{
    write(connection.handle(), out);
}

DumpReport dumpTo(Connection& connection, const std::string& path)
{
    const fs::path target = fs::absolute(fs::path(path));
    std::error_code error;
    if (!target.has_filename())
        throw DbError("a dump needs a file name, not a folder: " + path, SQLITE_CANTOPEN);
    fs::create_directories(target.parent_path(), error);
    if (error)
        throw DbError("cannot create " + target.parent_path().string() + ": " + error.message(), SQLITE_CANTOPEN);

    const fs::path partial = target.string() + ".partial";
    struct Cleanup {
        const fs::path& path;
        bool keep = false;
        ~Cleanup()
        {
            if (!keep) {
                std::error_code ignored;
                fs::remove(path, ignored);
            }
        }
    } cleanup {partial};

    std::ostringstream text;
    writeDump(connection, text);
    const std::string sql = text.str();
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        file << sql;
        file.close();
        if (!file)
            throw DbError("cannot write " + partial.string(), SQLITE_IOERR);
    }

    // Restore it where nothing else can interfere, and compare.
    sqlite3* restored = nullptr;
    sqlite3_open(":memory:", &restored);
    char* message = nullptr;
    const int rc = sqlite3_exec(restored, sql.c_str(), nullptr, nullptr, &message);
    std::string problem = message ? message : "";
    sqlite3_free(message);
    std::map<std::string, std::int64_t> restoredCounts;
    std::string again;
    if (rc == SQLITE_OK) {
        restoredCounts = rowCounts(restored);
        std::ostringstream redump;
        write(restored, redump);
        again = redump.str();
    }
    sqlite3_close(restored);
    if (rc != SQLITE_OK)
        throw DbError("the dump does not restore: " + problem, rc);

    // The restored catalogue must dump to the very same text: every value
    // came back as it was, not merely every row.
    const auto liveCounts = rowCounts(connection.handle());
    if (restoredCounts != liveCounts || again != sql)
        throw DbError("the restored dump does not match the catalogue", SQLITE_MISMATCH);

    fs::rename(partial, target, error);
    if (error)
        throw DbError("the dump could not be put in place: " + error.message(), SQLITE_CANTOPEN);
    cleanup.keep = true;

    DumpReport report;
    report.path = target.string();
    report.books = liveCounts.count("book") ? liveCounts.at("book") : 0;
    for (const auto& [table, count] : liveCounts)
        report.rows += count;
    return report;
}

} // namespace pinax::db
