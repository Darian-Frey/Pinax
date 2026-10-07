#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace pinax::db {
class Connection;
}

namespace pinax::io {

// These books, in this order, as CSV in the import format of SPEC.md §1
// (F-023): the importer's columns in its order, RFC 4180 quoting. A book in
// several series is a row per series, which the importer reads back as one
// book in each; `times_read` and `sort_position` are always written, so
// nothing is re-derived on the way in (AV-005, AV-006). Carries only what
// the format carries: not synopsis, genres, covers, ISBN-10 or dates.
void writeBooksCsv(db::Connection& connection, const std::vector<std::int64_t>& bookIds, std::ostream& out);

struct CsvExportReport {
    int books = 0;
    int rows = 0;
    std::string path;
};

// Writes the CSV to a partial file beside `path`, imports it into an empty
// catalogue and exports that again, and keeps it — renamed over `path` —
// only if the two are identical: proof the file re-imports without loss.
// Returns why not, if not; `path` is then untouched.
std::optional<std::string> exportBooksCsv(db::Connection& connection, const std::vector<std::int64_t>& bookIds,
    const std::string& path, CsvExportReport& report);

} // namespace pinax::io
