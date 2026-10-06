#pragma once

#include "domain/series_membership.h"
#include "domain/series_status.h"

#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// Completeness across the whole catalogue, for the panel's "across the
// library" figures. Derived, like everything about completeness (D-004).
struct LibrarySeriesTotals {
    int oneVolumeShort = 0;
    int complete = 0; // Complete and Complete to date
    int withGaps = 0;
    int volumesNotOwned = 0;

    bool operator==(const LibrarySeriesTotals&) const = default;
};

// Everything the detail panel shows about one series.
struct SeriesDetail {
    SeriesStatus series;
    std::optional<std::string> authors; // the authors of its owned volumes, most frequent first
    std::vector<MissingVolume> missing; // in series order
    LibrarySeriesTotals library;

    bool operator==(const SeriesDetail&) const = default;
};

} // namespace pinax::domain
