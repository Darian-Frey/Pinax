#pragma once

#include "domain/enums.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// One row of `v_book_display`: what the list view shows and sorts by.
// Read-only; edits go through Book.
struct BookSummary {
    std::int64_t id = 0;
    std::string title;
    std::string sortTitle;

    std::optional<std::string> authors;    // 'Larry Niven & Jerry Pournelle'
    std::optional<std::string> authorSort; // 'Niven, Larry'

    std::optional<std::string> seriesLabel;       // 'The Culture · 5'
    std::optional<std::string> seriesSort;        // 'The Culture'
    std::optional<double> seriesSortPosition;     // 5.0; never parsed from the label

    ReadStatus readStatus = ReadStatus::Unread;
    int timesRead = 0;
    std::optional<std::string> dateFinished;
    std::optional<int> rating;
    std::optional<int> publishedYear;

    std::optional<std::string> coverPath;
    MetadataStatus metadataStatus = MetadataStatus::Unmatched;

    bool operator==(const BookSummary&) const = default;
};

} // namespace pinax::domain
