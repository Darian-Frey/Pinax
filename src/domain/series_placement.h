#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// One series a book is in, as the edit form gives it (BUG-005): the series —
// one already in the catalogue, or a new name — the position as printed, and
// the sort number, which may be left for the importer's rule to derive
// (AV-006).
struct SeriesPlacement {
    std::optional<std::int64_t> seriesId; // chosen from the catalogue's series
    std::string seriesName;               // typed: an existing name, or a new series
    std::optional<std::string> position;
    std::optional<double> sortPosition;

    bool operator==(const SeriesPlacement&) const = default;
};

} // namespace pinax::domain
