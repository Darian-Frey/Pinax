#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// One row of v_missing_entries: a volume a series lacks, and how many its
// series lacks in all — the shopping list (F-010).
struct MissingRow {
    std::int64_t entryId = 0;
    std::int64_t seriesId = 0;
    std::string seriesName;
    std::optional<std::string> position;
    std::optional<double> sortPosition;
    std::optional<std::string> title;
    int missingInSeries = 0;

    bool operator==(const MissingRow&) const = default;
};

} // namespace pinax::domain
