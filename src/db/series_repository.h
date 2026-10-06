#pragma once

#include "domain/series_entry.h"
#include "domain/series_detail.h"
#include "domain/series_membership.h"
#include "domain/series_row.h"
#include "domain/series_status.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

    // The first entry in this series at exactly this printed position,
    // owned or not.
    std::optional<domain::SeriesEntry> entryAt(std::int64_t seriesId, const std::string& position);

    // The first entry in this series with no position and this title,
    // compared without regard to ASCII case. Owned entries are matched on
    // their entry title only, not the book's.
    std::optional<domain::SeriesEntry> unpositionedEntryTitled(std::int64_t seriesId,
        const std::string& title);

    std::optional<bool> ongoing(std::int64_t seriesId);
    void setOngoing(std::int64_t seriesId, bool ongoing);

    // Every series the book belongs to, by series name, with completeness and
    // missing volumes read from v_series_status and v_missing_entries.
    std::vector<domain::SeriesMembership> membershipsForBook(std::int64_t bookId);

    // Every series with its completeness, from v_series_status, by name as
    // stored; filing order is the caller's.
    std::vector<domain::SeriesStatus> statuses();

    // Every entry of the series, owned or not, in series order: by
    // sort_position, entries with none last, then by printed position.
    std::vector<domain::SeriesRow> rows(std::int64_t seriesId);

    // One series' completeness, or nullopt if there is no such series.
    std::optional<domain::SeriesStatus> status(std::int64_t seriesId);

    // The volumes it is known to lack, in series order (v_missing_entries).
    std::vector<domain::MissingVolume> missing(std::int64_t seriesId);

    // Completeness across every series, from v_series_status.
    domain::LibrarySeriesTotals libraryTotals();

    // Ids of the books on the shelf in this series.
    std::vector<std::int64_t> bookIds(std::int64_t seriesId);

    std::optional<domain::SeriesEntry> findEntry(std::int64_t entryId);
    std::optional<std::string> name(std::int64_t seriesId);

    // The highest sort_position in the series, if any entry has one.
    std::optional<double> lastSortPosition(std::int64_t seriesId);

    // Deletes the entry. A book attached to it stays in the catalogue.
    bool removeEntry(std::int64_t entryId);

    std::int64_t addEntry(const domain::SeriesEntry& entry);
    bool updateEntry(const domain::SeriesEntry& entry);

    std::int64_t count();
    std::int64_t entryCount();

private:
    Connection& connection_;
};

} // namespace pinax::db
