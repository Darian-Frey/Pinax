#pragma once

#include "domain/book.h"

#include <cstdint>
#include <optional>

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

    // Writes every field of the book with this id. Moving `readStatus` into
    // Read increments `timesRead` and stamps `dateFinished` by trigger,
    // overriding the values passed (F-006). Returns false if no such book.
    bool update(const domain::Book& book);

    // Deletes the book; credits and genre links cascade, and its series
    // entries stay as missing volumes (F-001). Returns false if no such book.
    bool remove(std::int64_t id);

    std::int64_t count();

private:
    Connection& connection_;
};

} // namespace pinax::db
