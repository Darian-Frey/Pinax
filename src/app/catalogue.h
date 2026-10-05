#pragma once

#include "db/connection.h"
#include "domain/book_detail.h"
#include "domain/book_edit.h"
#include "domain/book_summary.h"

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
    // starts at one read (AV-005).
    SaveResult save(const domain::BookEdit& edit);

    // Deletes the books in one transaction (F-001). Credits and genre links
    // go with them; series entries stay as missing volumes. Returns nothing
    // on success, or a message.
    std::optional<std::string> remove(const std::vector<std::int64_t>& ids);

private:
    std::string path_;
    db::Connection connection_;
};

} // namespace pinax::app
