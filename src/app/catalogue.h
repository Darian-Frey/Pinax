#pragma once

#include "db/connection.h"
#include "domain/book_detail.h"
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

    // Writes the book's own fields. Returns nothing on success, or a message
    // in the owner's terms: a duplicate ISBN, a book deleted meanwhile.
    std::optional<std::string> save(const domain::Book& book);

private:
    std::string path_;
    db::Connection connection_;
};

} // namespace pinax::app
