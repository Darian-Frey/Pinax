#include "db/backup.h"

#include "db/connection.h"
#include "db/db_error.h"
#include "db/statement.h"

#include <sqlite3.h>

#include <filesystem>
#include <system_error>

namespace pinax::db {

namespace {

namespace fs = std::filesystem;

std::int64_t scalar(sqlite3* db, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    std::int64_t value = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
        value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

std::string text(sqlite3* db, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    std::string value;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW) {
        if (const auto* bytes = sqlite3_column_text(statement, 0))
            value = reinterpret_cast<const char*>(bytes);
    }
    sqlite3_finalize(statement);
    return value;
}

// Removes the temporary file however the backup ends.
struct Partial {
    fs::path path;
    bool keep = false;
    ~Partial()
    {
        if (!keep) {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }
};

} // namespace

BackupReport backupTo(Connection& live, const std::string& path)
{
    const fs::path target = fs::absolute(fs::path(path));
    std::error_code error;
    if (!target.has_filename())
        throw DbError("a backup needs a file name, not a folder: " + path, SQLITE_CANTOPEN);
    fs::create_directories(target.parent_path(), error);
    if (error)
        throw DbError("cannot create " + target.parent_path().string() + ": " + error.message(), SQLITE_CANTOPEN);

    Partial partial {target.string() + ".partial"};
    fs::remove(partial.path, error); // a leftover from an interrupted backup

    const std::int64_t books = scalar(live.handle(), "SELECT COUNT(*) FROM book");
    const std::int64_t version = scalar(live.handle(), "SELECT MAX(version) FROM schema_version");
    {
        Statement vacuum(live, "VACUUM INTO :path");
        vacuum.bind(":path", partial.path.string());
        vacuum.step();
    }

    // Read it back as a stranger would: read-only, nothing assumed.
    sqlite3* copy = nullptr;
    if (sqlite3_open_v2(partial.path.c_str(), &copy, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string message = copy ? sqlite3_errmsg(copy) : "out of memory";
        sqlite3_close(copy);
        throw DbError("the backup could not be opened: " + message, SQLITE_CANTOPEN);
    }
    const std::string integrity = text(copy, "PRAGMA quick_check");
    const std::int64_t copiedBooks = scalar(copy, "SELECT COUNT(*) FROM book");
    const std::int64_t copiedVersion = scalar(copy, "SELECT MAX(version) FROM schema_version");
    sqlite3_close(copy);
    if (integrity != "ok")
        throw DbError("the backup failed its integrity check: " + integrity, SQLITE_CORRUPT);
    if (copiedBooks != books || copiedVersion != version) {
        throw DbError("the backup does not match the catalogue: " + std::to_string(copiedBooks) + " books against "
                + std::to_string(books),
            SQLITE_MISMATCH);
    }

    fs::rename(partial.path, target, error);
    if (error)
        throw DbError("the backup could not be put in place: " + error.message(), SQLITE_CANTOPEN);
    partial.keep = true;
    return {copiedBooks, target.string()};
}

} // namespace pinax::db
