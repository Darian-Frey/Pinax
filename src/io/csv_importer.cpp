#include "io/csv_importer.h"

#include "db/author_repository.h"
#include "db/book_repository.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/savepoint.h"
#include "db/series_repository.h"
#include "db/transaction.h"
#include "domain/credit_text.h"
#include "domain/isbn.h"
#include "io/csv_reader.h"
#include "io/import_support.h"
#include "io/sort_position.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace pinax::io {

using domain::Book;
using domain::Credit;
using domain::CreditRole;
using domain::ReadStatus;

namespace {

// SPEC.md §1, in its column order.
constexpr std::array<std::string_view, 16> knownColumns{
    "title", "subtitle", "authors", "series", "position", "sort_position",
    "shelf", "times_read", "rating", "isbn13", "publisher", "published_year",
    "binding", "edition_note", "condition_note", "notes"};

using detail::optionalText;
using detail::parseNumber;
using detail::RowError;
using detail::trim;

using ParsedCredit = domain::NamedCredit;

// SPEC.md §1.1, through the parser the edit form shares.
std::vector<ParsedCredit> parseCredits(const std::string& cell)
{
    try {
        return domain::parseCredits(cell);
    } catch (const domain::CreditTextError& error) {
        throw RowError { error.what() };
    }
}

// One data row, validated, with each column either absent from the file
// (nullopt) or present with its value.
struct Row {
    int line = 0;
    std::map<std::string_view, std::string> cells;

    bool has(std::string_view column) const { return cells.contains(column); }
    const std::string& cell(std::string_view column) const { return cells.at(column); }

    std::string title;
    std::optional<std::vector<ParsedCredit>> credits;
    std::optional<std::string> series;
    std::optional<std::string> position;
    std::optional<double> sortPosition;
    std::optional<ReadStatus> shelf;
    std::optional<int> timesRead;
    std::optional<int> rating;
    std::optional<std::string> isbn13;
    std::optional<int> publishedYear;
    std::optional<domain::Binding> binding;
};

Row parseRow(const CsvRecord& record, const std::vector<std::string_view>& header)
{
    if (record.fields.size() != header.size()) {
        throw RowError { "expected " + std::to_string(header.size()) + " fields, found "
            + std::to_string(record.fields.size()) };
    }

    Row row;
    row.line = record.line;
    for (std::size_t i = 0; i < header.size(); ++i)
        row.cells[header[i]] = trim(record.fields[i]);

    row.title = row.cell("title");
    if (row.title.empty())
        throw RowError { "title is empty" };

    if (row.has("authors") && !row.cell("authors").empty())
        row.credits = parseCredits(row.cell("authors"));
    else if (row.has("authors"))
        row.credits = std::vector<ParsedCredit> {};

    if (row.has("series"))
        row.series = optionalText(row.cell("series"));
    if (row.has("position"))
        row.position = optionalText(row.cell("position"));
    if (row.position && !row.series)
        throw RowError { "position '" + *row.position + "' given without a series" };
    if (row.has("sort_position"))
        row.sortPosition = parseNumber<double>(row.cell("sort_position"), "sort_position");
    if (!row.sortPosition && row.position)
        row.sortPosition = deriveSortPosition(*row.position);

    if (row.has("shelf") && !row.cell("shelf").empty()) {
        row.shelf = domain::readStatusFromString(row.cell("shelf"));
        if (!row.shelf)
            throw RowError { "shelf '" + row.cell("shelf") + "' is not one of unread, reading, read, abandoned" };
    }

    if (row.has("times_read")) {
        row.timesRead = parseNumber<int>(row.cell("times_read"), "times_read");
        if (row.timesRead && *row.timesRead < 0)
            throw RowError { "times_read cannot be negative" };
    }

    if (row.has("rating")) {
        row.rating = parseNumber<int>(row.cell("rating"), "rating");
        // F-007: rejected, not clamped.
        if (row.rating && (*row.rating < 1 || *row.rating > 10))
            throw RowError { "rating " + std::to_string(*row.rating) + " is outside 1 to 10" };
    }

    if (row.has("isbn13") && !row.cell("isbn13").empty()) {
        const std::string isbn = domain::normaliseIsbn(row.cell("isbn13"));
        if (!domain::isValidIsbn13(isbn))
            throw RowError { "isbn13 '" + row.cell("isbn13") + "' fails its check digit" };
        row.isbn13 = isbn;
    }

    if (row.has("published_year"))
        row.publishedYear = parseNumber<int>(row.cell("published_year"), "published_year");

    if (row.has("binding") && !row.cell("binding").empty()) {
        row.binding = domain::bindingFromString(row.cell("binding"));
        if (!row.binding)
            throw RowError { "binding '" + row.cell("binding") + "' is not recognised" };
    }
    return row;
}

// Writes the file's values onto `book` for every column the file has. A
// column the file lacks leaves the field alone; an empty cell clears it.
void applyRow(const Row& row, Book& book)
{
    book.title = row.title;
    if (row.has("subtitle"))
        book.subtitle = optionalText(row.cell("subtitle"));
    if (row.shelf)
        book.readStatus = *row.shelf;
    if (row.timesRead)
        book.timesRead = *row.timesRead;
    if (row.has("rating"))
        book.rating = row.rating;
    if (row.has("isbn13"))
        book.isbn13 = row.isbn13;
    if (row.has("publisher"))
        book.publisher = optionalText(row.cell("publisher"));
    if (row.has("published_year"))
        book.publishedYear = row.publishedYear;
    if (row.has("binding"))
        book.binding = row.binding;
    if (row.has("edition_note"))
        book.editionNote = optionalText(row.cell("edition_note"));
    if (row.has("condition_note"))
        book.conditionNote = optionalText(row.cell("condition_note"));
    if (row.has("notes"))
        book.notes = optionalText(row.cell("notes"));
}

std::optional<std::string> firstAuthor(const Row& row)
{
    if (!row.credits)
        return std::nullopt;
    for (const ParsedCredit& credit : *row.credits) {
        if (credit.role == CreditRole::Author)
            return credit.name;
    }
    return std::nullopt;
}

class RowImporter {
public:
    explicit RowImporter(db::Connection& connection)
        : books_(connection)
        , authors_(connection)
        , series_(connection)
    {
    }

    enum class Outcome { Inserted, Updated, Unchanged };

    // Called once the row's savepoint is released. Until then its book may
    // still be rolled back, and SQLite would reuse a rolled-back id.
    void rowCommitted(int line)
    {
        if (pendingBookId_) {
            linesWritten_.try_emplace(*pendingBookId_, line);
            if (pendingSeries_)
                seriesGiven_[*pendingBookId_].insert(*pendingSeries_);
        }
        pendingBookId_.reset();
        pendingSeries_.reset();
    }

    Outcome import(const Row& row)
    {
        pendingBookId_.reset();
        pendingSeries_ = row.series;
        bool changed = false;
        const std::int64_t bookId = writeBook(row, changed);
        if (row.credits)
            writeCredits(bookId, *row.credits, changed);
        if (row.series)
            writeSeriesEntry(bookId, row, changed);

        if (inserted_)
            return Outcome::Inserted;
        return changed ? Outcome::Updated : Outcome::Unchanged;
    }

private:
    std::optional<Book> match(const Row& row)
    {
        if (row.isbn13) {
            if (auto byIsbn = books_.findByIsbn13(*row.isbn13))
                return byIsbn;
        }
        // Without an ISBN match, fall back to title and author — but never
        // onto a book that already carries a different ISBN.
        auto byTitle = books_.findByTitleAndFirstAuthor(row.title, firstAuthor(row));
        if (byTitle && row.isbn13 && byTitle->isbn13 && *byTitle->isbn13 != *row.isbn13)
            return std::nullopt;
        return byTitle;
    }

    std::int64_t writeBook(const Row& row, bool& changed)
    {
        inserted_ = false;
        const std::optional<Book> existing = match(row);

        // Two rows of one file resolving to one book is a mistake in the
        // file; letting the second overwrite the first would hide it. The
        // one exception (D-026): a row that changes nothing about the book
        // and names a series not yet given for it — a book in several
        // series is a row per series, as the export writes it.
        if (existing) {
            if (const auto earlier = linesWritten_.find(existing->id); earlier != linesWritten_.end()) {
                Book book = *existing;
                applyRow(row, book);
                const auto given = seriesGiven_.find(existing->id);
                const bool newSeries = row.series
                    && (given == seriesGiven_.end() || !given->second.count(*row.series));
                if (!(book == *existing && sameCredits(row, existing->id) && newSeries)) {
                    throw RowError { "same book as line " + std::to_string(earlier->second)
                        + " (matched on " + (row.isbn13 && existing->isbn13 == row.isbn13 ? "ISBN" : "title and author")
                        + "); a further row for a book may only add a series" };
                }
                pendingBookId_ = existing->id;
                return existing->id;
            }
        }

        if (!existing) {
            Book book;
            applyRow(row, book);
            // AV-005: the trigger only counts transitions, so an inserted read
            // book states its count. A file that gives none means once.
            if (!row.timesRead)
                book.timesRead = book.readStatus == ReadStatus::Read ? 1 : 0;
            inserted_ = true;
            changed = true;
            pendingBookId_ = books_.create(book);
            return *pendingBookId_;
        }

        pendingBookId_ = existing->id;
        Book book = *existing;
        applyRow(row, book);
        if (book == *existing)
            return existing->id;

        books_.update(book);
        changed = true;

        // Moving into Read fires the re-read trigger, which overrides the
        // count with the old one plus one. The file's count wins.
        if (row.timesRead) {
            Book stored = *books_.find(existing->id);
            if (stored.timesRead != *row.timesRead) {
                stored.timesRead = *row.timesRead;
                books_.update(stored);
            }
        }
        return existing->id;
    }

    // Whether the row's credits are the book's as they stand, without
    // creating any author.
    bool sameCredits(const Row& row, std::int64_t bookId)
    {
        if (!row.credits)
            return true;
        std::vector<Credit> credits;
        for (std::size_t i = 0; i < row.credits->size(); ++i) {
            const auto author = authors_.findByName((*row.credits)[i].name);
            if (!author)
                return false;
            credits.push_back({author->id, (*row.credits)[i].role, static_cast<int>(i)});
        }
        return credits == books_.credits(bookId);
    }

    void writeCredits(std::int64_t bookId, const std::vector<ParsedCredit>& parsed, bool& changed)
    {
        std::vector<Credit> credits;
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            Credit credit;
            credit.authorId = authors_.findOrCreate(parsed[i].name);
            credit.role = parsed[i].role;
            credit.ordinal = static_cast<int>(i);
            credits.push_back(credit);
        }
        if (credits == books_.credits(bookId))
            return;
        books_.setCredits(bookId, credits);
        changed = true;
    }

    void writeSeriesEntry(std::int64_t bookId, const Row& row, bool& changed)
    {
        const std::int64_t seriesId = series_.findOrCreate(*row.series);

        if (auto entry = series_.entryForBook(seriesId, bookId)) {
            if (entry->position == row.position && entry->sortPosition == row.sortPosition)
                return;
            entry->position = row.position;
            entry->sortPosition = row.sortPosition;
            series_.updateEntry(*entry);
            changed = true;
            return;
        }

        // AV-007: a volume already recorded as missing takes the book rather
        // than gaining a twin.
        if (row.position) {
            if (auto waiting = series_.unownedEntryAt(seriesId, *row.position)) {
                waiting->bookId = bookId;
                if (row.sortPosition)
                    waiting->sortPosition = row.sortPosition;
                series_.updateEntry(*waiting);
                changed = true;
                return;
            }
        }

        domain::SeriesEntry entry;
        entry.seriesId = seriesId;
        entry.bookId = bookId;
        entry.position = row.position;
        entry.sortPosition = row.sortPosition;
        series_.addEntry(entry);
        changed = true;
    }

    db::BookRepository books_;
    db::AuthorRepository authors_;
    db::SeriesRepository series_;
    bool inserted_ = false;
    std::optional<std::string> pendingSeries_;
    std::map<std::int64_t, std::set<std::string>> seriesGiven_; // per book, this file
    std::optional<std::int64_t> pendingBookId_;
    std::unordered_map<std::int64_t, int> linesWritten_; // book id -> first line this run
};

} // namespace

CsvImporter::CsvImporter(db::Connection& connection)
    : connection_(connection)
{
}

ImportReport CsvImporter::importFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ImportReport report;
        report.aborted = true;
        report.failures.push_back({ 0, "cannot read " + path });
        return report;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return importText(contents.str());
}

ImportReport CsvImporter::importText(std::string_view csv)
{
    ImportReport report;
    auto abort = [&](int line, std::string message) {
        report = ImportReport {};
        report.aborted = true;
        report.failures.push_back({ line, std::move(message) });
        return report;
    };

    std::vector<CsvRecord> records;
    try {
        records = parseCsv(csv);
    } catch (const CsvError& error) {
        return abort(error.line(), error.what());
    }
    if (records.empty())
        return abort(0, "the file is empty");

    // The header decides the whole run: an unknown or repeated column is
    // almost always a typo, and guessing would write the wrong field.
    std::vector<std::string_view> header;
    for (const std::string& raw : records.front().fields) {
        const std::string name = trim(raw);
        const auto known = std::find(knownColumns.begin(), knownColumns.end(), name);
        if (known == knownColumns.end())
            return abort(records.front().line, "unknown column '" + name + "'");
        if (std::find(header.begin(), header.end(), *known) != header.end())
            return abort(records.front().line, "column '" + name + "' appears twice");
        header.push_back(*known);
    }
    if (std::find(header.begin(), header.end(), "title") == header.end())
        return abort(records.front().line, "no title column");

    try {
        db::Transaction transaction(connection_);
        RowImporter importer(connection_);

        for (std::size_t i = 1; i < records.size(); ++i) {
            const CsvRecord& record = records[i];
            db::Savepoint savepoint(connection_, "import_row");
            try {
                switch (importer.import(parseRow(record, header))) {
                case RowImporter::Outcome::Inserted: ++report.inserted; break;
                case RowImporter::Outcome::Updated: ++report.updated; break;
                case RowImporter::Outcome::Unchanged: ++report.unchanged; break;
                }
                savepoint.release();
                importer.rowCommitted(record.line);
            } catch (const RowError& error) {
                report.failures.push_back({ record.line, error.message });
            } catch (const db::DbError& error) {
                // A constraint the file broke (a duplicate ISBN, say) belongs
                // to the row; anything else ends the run.
                if (!error.isConstraintViolation())
                    throw;
                report.failures.push_back({ record.line, error.what() });
            }
        }
        // A corrected credit leaves its old spelling credited by nothing
        // (IMP-003).
        db::AuthorRepository(connection_).removeUncredited();
        transaction.commit();
    } catch (const db::DbError& error) {
        return abort(0, std::string("database error, nothing imported: ") + error.what());
    }
    return report;
}

} // namespace pinax::io
