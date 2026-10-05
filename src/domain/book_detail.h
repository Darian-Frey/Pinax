#pragma once

#include "domain/book.h"
#include "domain/series_membership.h"

#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// Everything the detail panel shows about one book.
struct BookDetail {
    Book book;
    std::optional<std::string> authors; // credited authors in cover order
    std::vector<SeriesMembership> series;
    std::optional<std::string> coverFile; // absolute path, when the file exists

    bool operator==(const BookDetail&) const = default;
};

} // namespace pinax::domain
