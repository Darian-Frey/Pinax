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

CatalogueFile inspect(const std::string& path)
{
    CatalogueFile file;
    std::error_code error;
    file.exists = fs::exists(path, error);
    file.empty = !file.exists || fs::file_size(path, error) == 0;
    if (file.empty) {
        file.problem = file.exists ? "the file is empty" : "there is no such file";
        return file;
    }
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        file.problem = db ? sqlite3_errmsg(db) : "it cannot be opened";
        sqlite3_close(db);
        return file;
    }
    const std::int64_t hasVersions
        = scalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_version'");
    if (hasVersions < 0) {
        file.problem = "it is not a database";
    } else if (hasVersions == 0) {
        file.problem = "it is a database, but not a Pinax catalogue";
    } else {
        file.catalogue = true;
        file.version = static_cast<int>(scalar(db, "SELECT MAX(version) FROM schema_version"));
        const std::int64_t books = scalar(db, "SELECT COUNT(*) FROM book");
        file.books = books < 0 ? 0 : books;
    }
    sqlite3_close(db);
    return file;
}

BackupReport restoreFrom(const std::string& source, const std::string& target)
{
    const CatalogueFile from = inspect(source);
    if (!from.catalogue)
        throw DbError(source + " cannot be restored: " + from.problem, SQLITE_NOTADB);
    const fs::path to = fs::absolute(fs::path(target));
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    Partial partial {to.string() + ".partial"};
    fs::remove(partial.path, error);

    // A consistent copy of the source, even one that is itself in WAL mode.
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(source.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string message = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        throw DbError("cannot open " + source + ": " + message, SQLITE_CANTOPEN);
    }
    sqlite3_stmt* vacuum = nullptr;
    sqlite3_prepare_v2(db, "VACUUM INTO ?1", -1, &vacuum, nullptr);
    sqlite3_bind_text(vacuum, 1, partial.path.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = vacuum ? sqlite3_step(vacuum) : SQLITE_ERROR;
    sqlite3_finalize(vacuum);
    const std::string message = sqlite3_errmsg(db);
    sqlite3_close(db);
    if (rc != SQLITE_DONE)
        throw DbError("the backup could not be copied: " + message, rc);

    const CatalogueFile copied = inspect(partial.path.string());
    sqlite3* check = nullptr;
    sqlite3_open_v2(partial.path.c_str(), &check, SQLITE_OPEN_READONLY, nullptr);
    const std::string integrity = text(check, "PRAGMA quick_check");
    sqlite3_close(check);
    if (!copied.catalogue || integrity != "ok" || copied.books != from.books || copied.version != from.version)
        throw DbError("the restored copy did not check out: " + (integrity == "ok" ? copied.problem : integrity),
            SQLITE_CORRUPT);

    // The old catalogue's log would otherwise be replayed into the new one.
    fs::remove(to.string() + "-wal", error);
    fs::remove(to.string() + "-shm", error);
    fs::rename(partial.path, to, error);
    if (error)
        throw DbError("the restored catalogue could not be put in place: " + error.message(), SQLITE_CANTOPEN);
    partial.keep = true;
    return {copied.books, to.string()};
}

} // namespace pinax::db
