#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// Where a book being added by ISBN would go in a series (F-024, D-012),
// proposed for the owner to accept or not.
struct SeriesProposal {
    enum class Kind {
        FillsMissing, // takes a volume the series lacks (AV-007)
        JoinsSeries,  // a new volume of a tracked series, as the provider places it
    };
    Kind kind = Kind::FillsMissing;
    std::int64_t seriesId = 0;
    std::string seriesName;
    std::optional<std::int64_t> entryId;     // FillsMissing: the waiting entry
    std::optional<std::string> position;     // as printed
    std::optional<double> sortPosition;      // JoinsSeries: proposed, editable
    std::optional<std::string> entryTitle;   // FillsMissing: the volume's title
    int heldAfter = 0;                       // the series' completeness once added
    int knownAfter = 0;

    bool operator==(const SeriesProposal&) const = default;
};

} // namespace pinax::domain
