#pragma once

#include "domain/book.h"
#include "domain/book_summary.h"
#include "domain/credit.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pinax::db {

class Connection;

// Reads and writes `book` rows as domain::Book. Constraint violations — a
// rating outside 1-10, a duplicate ISBN-13 — surface as DbError with a
// SQLITE_CONSTRAINT_* code; nothing is clamped or silently corrected (F-007).
class BookRepository {
public:
    explicit BookRepository(Connection& connection);

    // Inserts the book and returns its id. `id`, `createdAt` and `updatedAt`
    // are ignored; an empty `sortTitle` is derived from the title.
    // `timesRead` is written as given: inserting a book already read does not
    // fire the re-read trigger, so the caller states the count (AV-005).
    std::int64_t create(const domain::Book& book);

    std::optional<domain::Book> find(std::int64_t id);

    std::optional<domain::Book> findByIsbn13(const std::string& isbn13);
    std::optional<domain::Book> findByIsbn10(const std::string& isbn10);

    // The import match of SPEC.md §1.3: title compared without regard to
    // case, plus the first-billed author's name likewise; nullopt matches a
    // book with no author credit. ASCII case only, as SQLite's lower() is.
    // The lowest id wins if several match.
    std::optional<domain::Book> findByTitleAndFirstAuthor(const std::string& title,
        const std::optional<std::string>& firstAuthor);

    // Every credit on the book, in cover order.
    std::vector<domain::Credit> credits(std::int64_t bookId);

    // Replaces every credit on the book with these, in the order given.
    void setCredits(std::int64_t bookId, const std::vector<domain::Credit>& credits);

    // Writes every field of the book with this id. Moving `readStatus` into
    // Read increments `timesRead` and stamps `dateFinished` by trigger,
    // overriding the values passed (F-006). Returns false if no such book.
    bool update(const domain::Book& book);

    // Deletes the book; credits and genre links cascade, and its series
    // entries stay as missing volumes (F-001). Returns false if no such book.
    bool remove(std::int64_t id);

    std::int64_t count();

    // How many books are in this read state.
    std::int64_t countWithReadStatus(domain::ReadStatus status);

    // Ids of the books in this read state.
    std::vector<std::int64_t> idsWithReadStatus(domain::ReadStatus status);

    // One book as the list view shows it.
    std::optional<domain::BookSummary> summary(std::int64_t id);

    // Every book as the list view shows it, read from v_book_display so that
    // flattening and sort keys are the database's, not recomputed here.
    // Unordered; the view sorts.
    std::vector<domain::BookSummary> summaries();

private:
    Connection& connection_;
};

} // namespace pinax::db
