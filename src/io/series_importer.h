#pragma once

#include "io/csv_importer.h"

#include <string>
#include <string_view>

namespace pinax::db {
class Connection;
}

namespace pinax::io {

// Imports the series CSV of SPEC.md §1.6 (D-018): volumes a series is known
// to contain but the shelf does not, and which series are still being
// written. Each row is a known volume, a series flag, or both.
//
// Idempotent: a volume is matched within its series on its printed
// position, or on its title when it has none, and is only written where the
// file differs. A volume already on the shelf at that position is left
// alone — never duplicated, never detached (AV-007). Nothing is ever removed.
//
// Same error handling as CsvImporter: one transaction, a savepoint per row,
// failures reported by line, the header checked before anything is written.
class SeriesImporter {
public:
    explicit SeriesImporter(db::Connection& connection);

    ImportReport importFile(const std::string& path);
    ImportReport importText(std::string_view csv);

private:
    db::Connection& connection_;
};

} // namespace pinax::io
