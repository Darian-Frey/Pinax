#pragma once

#include "domain/enums.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// One entry of a series as the series list shows it: an owned volume with its
// book's reading facts, or a volume known but not on the shelf (D-006).
struct SeriesRow {
    std::int64_t entryId = 0;
    std::optional<std::string> position; // as printed (D-005)
    std::optional<double> sortPosition;
    std::optional<std::string> entryTitle;

    std::optional<std::int64_t> bookId; // nullopt: not owned
    std::optional<std::string> bookTitle;
    ReadStatus readStatus = ReadStatus::Unread;
    int timesRead = 0;
    std::optional<int> rating;
    std::optional<int> publishedYear;

    bool owned() const { return bookId.has_value(); }

    // The book's title when owned, else the entry's.
    std::optional<std::string> title() const { return owned() && bookTitle ? bookTitle : entryTitle; }

    bool operator==(const SeriesRow&) const = default;
};

} // namespace pinax::domain
