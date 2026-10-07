#pragma once

#include "domain/enums.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// The book list's filters, combined (F-017): a book is shown when it meets
// every one that is set. Unset is "any".
struct BookQuery {
    std::optional<ReadStatus> readStatus;
    // Rating: unrated books only, or a range from..to (inclusive, 1-10).
    bool unratedOnly = false;
    std::optional<int> ratingFrom;
    std::optional<int> ratingTo;
    std::optional<std::int64_t> genreId;
    std::optional<std::int64_t> authorId; // credited as author, not editor (IMP-004)
    std::optional<std::int64_t> seriesId;
    // Words to find in a book's title, subtitle, credited names and series
    // (F-019), all of them, anywhere. Matched in the list, not in SQL.
    std::string text;

    bool empty() const
    {
        return !readStatus && !unratedOnly && !ratingFrom && !ratingTo && !genreId && !authorId && !seriesId
            && text.empty();
    }

    bool operator==(const BookQuery&) const = default;
};

// One choice in a filter: a genre, an author or a series, with how many
// books it would show on its own.
struct FilterOption {
    std::int64_t id = 0;
    std::string name;
    int count = 0;

    bool operator==(const FilterOption&) const = default;
};

} // namespace pinax::domain
