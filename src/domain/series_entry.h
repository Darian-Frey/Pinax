#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// One volume a series is known to contain (D-006). No book attached means a
// known volume missing from the shelf.
struct SeriesEntry {
    std::int64_t id = 0;
    std::int64_t seriesId = 0;
    std::optional<std::int64_t> bookId;

    std::optional<std::string> position; // as printed; never parsed (D-005)
    std::optional<double> sortPosition;  // ordering only

    std::optional<std::string> title;
    std::optional<std::string> notes;

    bool operator==(const SeriesEntry&) const = default;
};

} // namespace pinax::domain
