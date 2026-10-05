#pragma once

#include "domain/book.h"
#include "domain/credit_text.h"

#include <vector>

namespace pinax::domain {

// What the edit form hands back: the book's own fields and its credits by
// name, in cover order. `book.id` is 0 for a book not yet in the catalogue.
struct BookEdit {
    Book book;
    std::vector<NamedCredit> credits;

    bool operator==(const BookEdit&) const = default;
};

} // namespace pinax::domain
