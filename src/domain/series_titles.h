#pragma once

#include "domain/series_entry.h"
#include "domain/series_row.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// Finding the titles of a series' volumes the owner has not named (F-030,
// D-031): what a provider says the series contains, and what to propose.

// One volume of a series as a provider lists it.
struct FoundVolume {
    std::string title;
    std::optional<std::string> ordinal; // the provider's number, as given: "3", "0", "6.5"
    std::optional<int> year;
    std::vector<std::string> authors;

    bool operator==(const FoundVolume&) const = default;
};

// A provider's answer for one series: the series it took the name to mean,
// and its volumes in order.
struct SeriesFind {
    std::string provider;   // "Wikidata", "Open Library"
    std::string seriesName; // as the provider names it
    std::vector<FoundVolume> volumes;
    std::optional<std::string> error; // asked, and could not be answered

    bool operator==(const SeriesFind&) const = default;
};

// A found volume and where it would go: naming an unidentified entry, or a
// new entry of its own. Nothing is written until the owner accepts (AV-010).
struct TitleProposal {
    FoundVolume volume;
    std::optional<std::int64_t> entryId; // the unidentified entry it names; none: a new volume
    bool chosen = false;                 // ticked to begin with

    bool operator==(const TitleProposal&) const = default;
};

// An entry for a volume the shelf lacks and nobody has named: no title, or a
// placeholder's (D-018).
bool isUnidentified(const SeriesRow& row);
bool isUnidentified(const SeriesEntry& entry);

// What to propose for the found volumes, given the series as it stands.
// - A volume whose title the series already has is left out.
// - A numbered volume names the unidentified entry at the same position,
//   ticked. A volume whose title ends with an entry's position in words —
//   "Spinward Fringe Broadcast 10" for "Broadcast 10" — is that entry's:
//   it names it if unidentified, and is left out if not.
// - A numbered volume at a position the series already fills under another
//   title is left out: the owner's title stands.
// - A numbered volume whose position the series lacks takes the next
//   unidentified entry with no position — "Unidentified volume 1", a slot
//   for one volume the owner knew was missing — in order, ticked; the write
//   gives the entry the volume's number. "Later volumes — unidentified",
//   which stands for any number, is never taken this way.
// - Anything else is offered as a new volume, unticked.
// No entry is proposed twice.
std::vector<TitleProposal> planSeriesTitles(const std::vector<SeriesRow>& rows, const std::vector<FoundVolume>& found);

} // namespace pinax::domain
