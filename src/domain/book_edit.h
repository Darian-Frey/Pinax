#pragma once

#include "domain/book.h"
#include "domain/credit_text.h"
#include "domain/series_placement.h"

#include <optional>

#include <vector>

namespace pinax::domain {

// What the edit form hands back: the book's own fields and its credits by
// name, in cover order. `book.id` is 0 for a book not yet in the catalogue.
struct BookEdit {
    Book book;
    std::vector<NamedCredit> credits;
    // The series the book is in, in full (BUG-005); nullopt leaves them as
    // they are, as every path but the edit form does.
    std::optional<std::vector<SeriesPlacement>> series = std::nullopt;

    bool operator==(const BookEdit&) const = default;
};

} // namespace pinax::domain
