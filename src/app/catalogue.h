#pragma once

#include "db/connection.h"
#include "domain/book_detail.h"
#include "domain/book_edit.h"
#include "domain/book_filter.h"
#include "domain/book_summary.h"
#include "domain/book_query.h"
#include "domain/candidate.h"
#include "domain/series_proposal.h"
#include "domain/workbook.h"
#include "domain/missing_row.h"
#include "domain/series_detail.h"
#include "domain/series_entry.h"
#include "domain/series_row.h"
#include "domain/series_status.h"

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
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

    // The list's combined filters (F-017): the ids meeting them all, or
    // nullopt when none is set.
    std::optional<std::vector<std::int64_t>> matchingIds(const domain::BookQuery& query);
    // The choices for the filter bar, each with its count of books.
    std::vector<domain::FilterOption> genreOptions();
    std::vector<domain::FilterOption> authorOptions();
    std::vector<domain::FilterOption> seriesOptions();
    // What search reads for each book (F-019).
    std::map<std::int64_t, std::string> searchTexts();
    // Each book's genres by name, for grouping by genre (F-018).
    std::map<std::int64_t, std::vector<std::string>> genresByBook();

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
    // `ownersEdition`: an ISBN match, or a candidate the owner confirmed as
    // their edition (D-029) — only then are publisher and pages taken.
    EnrichResult enrich(std::int64_t bookId, const domain::Candidate& candidate, bool ownersEdition);

    // No provider knew the book: marked failed, its content untouched
    // (SPEC.md §3.4). A book whose metadata is marked manual stays so.
    void markLookupFailed(std::int64_t bookId);

    // Adding a book by ISBN (F-024, D-012). Nothing is written until addBook.

    // The book already holding this ISBN, as ISBN-13 or its ISBN-10 form.
    std::optional<domain::BookSummary> bookWithIsbn(const std::string& isbn13);

    // The provider's author names, each replaced by the catalogue's own
    // spelling where one plainly means the same person ("Iain Banks" ->
    // "Iain M. Banks"), as credits for the confirmation card.
    std::vector<domain::NamedCredit> creditsFor(const std::vector<std::string>& providerAuthors);

    // Where the book might go in a series: a missing volume whose title
    // agrees and whose series shares an author, or whose position matches
    // the provider's series statement; else a tracked series the provider
    // names, at the number it gives. Each with the series' completeness once
    // added.
    std::vector<domain::SeriesProposal> seriesProposals(const std::string& title,
        const std::vector<domain::NamedCredit>& credits, const domain::Candidate& candidate);

    // Books already held without an ISBN that look like this one — same
    // title, a shared author: most likely the very copy being scanned, from
    // the backlog imported without ISBNs.
    std::vector<domain::BookSummary> booksLike(const std::string& title,
        const std::vector<domain::NamedCredit>& credits);

    // Gives a held book the ISBN it was found by and writes the candidate's
    // details under the enrichment rules, as a fetch would (AV-001). Refused
    // if the book already has a different ISBN, or another book has this one.
    EnrichResult giveIsbn(std::int64_t bookId, const std::string& isbn13,
        const std::optional<std::string>& isbn10, const domain::Candidate& candidate, bool byIsbn);

    struct NewBook {
        domain::BookEdit edit;              // title, credits, ISBN and read state as confirmed
        domain::Candidate candidate;        // the rest, written under planEnrichment
        bool byIsbn = true;                 // the candidate answers this ISBN
        std::optional<domain::SeriesProposal> series; // accepted, perhaps with position edited
    };
    struct AddResult {
        std::int64_t id = 0;
        std::optional<std::string> coverUrl; // still to fetch
        std::optional<std::string> problem;
    };
    // Creates the book, writes the candidate's details under the enrichment
    // rules, and puts it in its series — the waiting entry, or a new one.
    // Refused if the ISBN is already held.
    AddResult addBook(const NewBook& book);

    // A checked copy of the catalogue at `path`, the live file left open
    // (F-020, AV-003): the number of books copied, or why not.
    struct BackupResult {
        std::int64_t books = 0;
        std::string path;
        std::optional<std::string> problem;
    };
    BackupResult backupTo(const std::string& path);

    // The catalogue as plain SQL (F-021), checked by restoring it: to a file,
    // or written to a stream as it stands.
    struct ExportResult {
        std::int64_t books = 0;
        std::string path;
        std::optional<std::string> problem;
    };
    ExportResult dumpTo(const std::string& path);
    void writeDump(std::ostream& out);

    // The catalogue as the three sheets of SPEC.md §5.1 — Books, Series
    // status, Authors — read from the views, so the workbook agrees with the
    // application by construction (F-022, AV-011).
    domain::Workbook workbook();
    // That workbook as an .xlsx file (F-022).
    ExportResult exportWorkbook(const std::string& path);

    // These books, in this order, as CSV in the import format (F-023),
    // checked by re-importing it.
    ExportResult exportCsv(const std::vector<std::int64_t>& bookIds, const std::string& path);
    // Every book in the list's opening order: author, then series and
    // position, then title.
    std::vector<std::int64_t> booksInListOrder();

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
    // Puts the book in exactly these series, inside the caller's
    // transaction (BUG-005): a place it holds is updated; a new one fills a
    // missing volume at the same position, or with the same title, before
    // adding an entry (AV-007); a series it is no longer in stops counting
    // it. A new series name creates the series. Returns why not, if not.
    std::optional<std::string> placeInSeries(std::int64_t bookId, const std::string& title,
        const std::vector<domain::SeriesPlacement>& placements);

    std::string path_;
    db::Connection connection_;
};

} // namespace pinax::app
