#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pinax::db {
class Connection;
}

namespace pinax::io {

struct ImportFailure {
    int line = 0; // physical line in the file; 0 for the file as a whole
    std::string message;
};

struct ImportReport {
    int inserted = 0;
    int updated = 0;
    int unchanged = 0;
    std::vector<ImportFailure> failures;

    // True when nothing was written: the file could not be read, its header
    // was unusable, or the run hit a database error and rolled back.
    bool aborted = false;
};

// Imports the CSV format of SPEC.md §1 (F-003).
//
// Idempotent (AV-002): a book is matched on ISBN-13, else on title and
// first-billed author, and a match is updated only where the file differs.
// Authors and series are reused by exact name. `times_read` is written
// explicitly, never left to the re-read trigger (AV-005). A series position
// that matches a known-but-unowned entry attaches the book to that entry
// rather than adding a second one (AV-007).
//
// The whole run is one transaction. A row that fails validation or a
// constraint is rolled back on its own, reported with its line number, and
// the rest carry on; anything else rolls back the run.
class CsvImporter {
public:
    explicit CsvImporter(db::Connection& connection);

    ImportReport importFile(const std::string& path);
    ImportReport importText(std::string_view csv);

private:
    db::Connection& connection_;
};

} // namespace pinax::io
