#pragma once

#include "db/connection.h"
#include "domain/book_detail.h"
#include "domain/book_edit.h"
#include "domain/book_filter.h"
#include "domain/book_summary.h"
#include "domain/candidate.h"
#include "domain/missing_row.h"
#include "domain/series_detail.h"
#include "domain/series_entry.h"
#include "domain/series_row.h"
#include "domain/series_status.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pinax::app {

// The open catalogue: one connection, migrated on open, and the reads and
// writes the window needs, put together from db's repositories. The window
// talks to this, never to SQL (invariant 8).
class Catalogue {
public:
    // Opens or creates the database and brings its schema up to date.
    // Throws db::DbError if either fails.
    explicit Catalogue(const std::string& path);

    const std::string& path() const { return path_; }
    db::Connection& connection() { return connection_; }

    std::int64_t count();
    std::vector<domain::BookSummary> summaries();
    std::optional<domain::BookSummary> summary(std::int64_t id);
    std::optional<domain::BookDetail> detail(std::int64_t id);

    // For the rail: how many books in a read state, and every series with
    // its completeness (D-004), filed by name with a leading article moved
    // to the end, as titles are.
    std::int64_t countWithReadStatus(domain::ReadStatus status);
    std::vector<domain::SeriesStatus> seriesStatuses();

    // A series' entries in series order, owned and missing (D-006), and what
    // the panel says about it; nullopt if there is no such series.
    std::vector<domain::SeriesRow> seriesRows(std::int64_t seriesId);
    std::optional<domain::SeriesDetail> seriesDetail(std::int64_t seriesId);

    // The shopping list (F-010): every missing volume, series needing fewest
    // first and filed as titles are within that; or only the series one
    // volume short.
    std::vector<domain::MissingRow> missingVolumes(bool oneVolumeShortOnly = false);
    domain::LibrarySeriesTotals libraryTotals();

    // The books a filter selects; nullopt for "all of them".
    std::optional<std::vector<std::int64_t>> bookIds(const domain::BookFilter& filter);

    // F-005. Marks every listed book read; or, if all of them are read
    // already, marks them all unread. Moving into read counts a read through
    // the trigger (F-006); unmarking takes that read back, so a toggle
    // pressed twice leaves the count where it was (D-017). One transaction.
    // Returns the state the books now have. Throws db::DbError.
    domain::ReadStatus toggleRead(const std::vector<std::int64_t>& ids);

    // F-007. Sets or clears the rating of every listed book in one
    // transaction. Throws db::DbError, including for a rating outside 1-10.
    void setRating(const std::vector<std::int64_t>& ids, std::optional<int> rating);

    // Writes the book's own fields. Returns nothing on success, or a message
    // in the owner's terms: a duplicate ISBN, a book deleted meanwhile.
    std::optional<std::string> save(const domain::Book& book);

    struct SaveResult {
        std::int64_t id = 0;                // the book written, 0 on failure
        std::optional<std::string> problem; // set on failure
    };

    // Writes an edit from the panel in one transaction: the book's fields and
    // its credits, resolving each name to an author (created if new, D-007).
    // Creates the book when `book.id` is 0 (F-001); a new book marked read
    // starts at one read (AV-005). Authors the change leaves uncredited, and
    // without notes, are removed (IMP-003). With `attachTo`, the book is
    // attached to that waiting series entry in the same transaction rather
    // than given a new one (AV-007).
    SaveResult save(const domain::BookEdit& edit, std::optional<std::int64_t> attachTo = std::nullopt);

    // Series entries (F-008, F-009, Phase 2 step 4).
    std::optional<domain::SeriesEntry> entry(std::int64_t entryId);
    std::optional<std::string> seriesName(std::int64_t seriesId);

    // A sort number for a new entry: one after the series' last, or 1.
    double nextSortPosition(std::int64_t seriesId);

    // Adds the entry when its id is 0, else updates it. Needs a position or
    // a title. Returns nothing on success, or a message.
    std::optional<std::string> saveEntry(const domain::SeriesEntry& entry);

    // Removes the entry. An owned volume's book stays in the catalogue, out
    // of this series.
    std::optional<std::string> removeEntry(std::int64_t entryId);

    // Marks a missing volume as owned by attaching a book already in the
    // catalogue (AV-007). Refused if the entry is owned already or the book
    // is in the series under another entry.
    std::optional<std::string> attach(std::int64_t entryId, std::int64_t bookId);

    // The credits a new volume of this series most likely carries: those of
    // its owned volume whose authors are most frequent there.
    std::vector<domain::NamedCredit> seriesCredits(std::int64_t seriesId);

    // Writes a confirmed candidate onto a book under the enrichment rules
    // (domain::planEnrichment, AV-001), adds its categories as genres with
    // their source, and marks the book matched — in one transaction. Returns
    // the cover URL still to fetch, if any; or a problem.
    struct EnrichResult {
        std::optional<std::string> coverUrl;
        std::optional<std::string> problem;
    };
    EnrichResult enrich(std::int64_t bookId, const domain::Candidate& candidate, bool fromIsbnLookup);

    // No provider knew the book: marked failed, its content untouched
    // (SPEC.md §3.4). A book whose metadata is marked manual stays so.
    void markLookupFailed(std::int64_t bookId);

    // The folder holding the database, where covers are kept beside it
    // (SPEC.md §4); nullopt for an in-memory catalogue.
    std::optional<std::string> dataDirectory() const;

    // Records a book's cover: its path relative to the data directory and
    // where it came from (F-013, F-015). A cover set by hand is never
    // replaced by a fetched one (AV-001). Returns nothing on success, or why.
    std::optional<std::string> setCover(std::int64_t bookId, const std::string& relativePath, domain::Source source);

    // Deletes the books in one transaction (F-001). Credits and genre links
    // go with them; series entries stay as missing volumes; authors left
    // uncredited, and without notes, go too (IMP-003); so do their cover
    // files, since a later book may be given the same id. Returns nothing on
    // success, or a message.
    std::optional<std::string> remove(const std::vector<std::int64_t>& ids);

private:
    std::string path_;
    db::Connection connection_;
};

} // namespace pinax::app
