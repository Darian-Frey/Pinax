#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// A volume the series is known to contain but the shelf does not (D-006).
struct MissingVolume {
    std::optional<std::string> position;
    std::optional<std::string> title;

    bool operator==(const MissingVolume&) const = default;
};

// One book's place in one series, with the series' completeness as
// v_series_status computes it (D-004). Read-only.
struct SeriesMembership {
    std::int64_t seriesId = 0;
    std::string name;
    bool ongoing = false;

    std::optional<std::string> position; // as printed
    std::optional<double> sortPosition;

    int held = 0;  // entries on the shelf
    int known = 0; // entries the series is known to contain
    int heldRead = 0;
    std::string status; // Complete | Complete to date | Incomplete | Unknown

    std::vector<MissingVolume> missing; // in series order

    bool operator==(const SeriesMembership&) const = default;
};

} // namespace pinax::domain
