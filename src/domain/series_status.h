#pragma once

#include <cstdint>
#include <string>

namespace pinax::domain {

// One row of v_series_status: a series and its completeness, derived from
// held against known entries (D-004). Read-only.
struct SeriesStatus {
    std::int64_t id = 0;
    std::string name;
    bool ongoing = false;
    int held = 0;
    int known = 0;
    int heldRead = 0;
    std::string status; // Complete | Complete to date | Incomplete | Unknown

    bool operator==(const SeriesStatus&) const = default;
};

} // namespace pinax::domain
