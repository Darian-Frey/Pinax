#pragma once

#include "domain/series_entry.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::db {

class Connection;

// Reads and writes `series` and `series_entry` rows. Completeness is never
// computed here; it is v_series_status's (D-004).
class SeriesRepository {
public:
    explicit SeriesRepository(Connection& connection);

    // The id of the series with exactly this name, creating it if absent.
    std::int64_t findOrCreate(const std::string& name);

    // The entry attaching this book to this series, if any.
    std::optional<domain::SeriesEntry> entryForBook(std::int64_t seriesId, std::int64_t bookId);

    // An entry in this series with no book attached and exactly this printed
    // position: a known volume waiting for its book (D-006, AV-007).
    std::optional<domain::SeriesEntry> unownedEntryAt(std::int64_t seriesId,
        const std::string& position);

    std::int64_t addEntry(const domain::SeriesEntry& entry);
    bool updateEntry(const domain::SeriesEntry& entry);

    std::int64_t count();
    std::int64_t entryCount();

private:
    Connection& connection_;
};

} // namespace pinax::db
