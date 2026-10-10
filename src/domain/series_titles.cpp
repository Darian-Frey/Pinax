#include "domain/series_titles.h"

#include "domain/enrichment.h"
#include "domain/placeholder.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace pinax::domain {

namespace {

std::string trimmed(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// "Spinward Fringe Broadcast 10" ends with the position "Broadcast 10", as a
// whole word; "Broadcast 1" is not the end of "Broadcast 10". Only a
// position with words in it counts: a bare "1" ends too many titles.
bool endsWithPosition(const std::string& title, const std::string& position)
{
    const std::string t = lower(trimmed(title));
    const std::string p = lower(trimmed(position));
    if (p.empty() || t.size() <= p.size() || !t.ends_with(p))
        return false;
    if (std::none_of(p.begin(), p.end(), [](unsigned char c) { return std::isalpha(c); }))
        return false;
    const char before = t[t.size() - p.size() - 1];
    return !std::isalnum(static_cast<unsigned char>(before));
}

} // namespace

namespace {

bool unnamed(const std::optional<std::string>& title)
{
    return !title || trimmed(*title).empty() || isPlaceholderTitle(title);
}

} // namespace

bool isUnidentified(const SeriesRow& row)
{
    return !row.owned() && unnamed(row.entryTitle);
}

bool isUnidentified(const SeriesEntry& entry)
{
    return !entry.bookId && unnamed(entry.title);
}

std::vector<TitleProposal> planSeriesTitles(const std::vector<SeriesRow>& rows, const std::vector<FoundVolume>& found)
{
    std::vector<TitleProposal> proposals;
    std::set<std::int64_t> taken;

    auto known = [&](const std::string& title) {
        for (const auto& row : rows) {
            if (isUnidentified(row))
                continue;
            const auto own = row.title();
            if (own && (titlesAgree(*own, title) || titlesAgree(title, *own)))
                return true;
        }
        for (const auto& proposal : proposals) {
            if (titlesAgree(proposal.volume.title, title) || titlesAgree(title, proposal.volume.title))
                return true;
        }
        return false;
    };

    // Slots: unidentified entries with no position, one volume each.
    std::vector<std::int64_t> slots;
    for (const auto& row : rows) {
        if (isUnidentified(row) && (!row.position || trimmed(*row.position).empty())
            && !isOpenEndedPlaceholder(row.entryTitle))
            slots.push_back(row.entryId);
    }
    std::size_t nextSlot = 0;

    for (const auto& volume : found) {
        if (trimmed(volume.title).empty() || known(volume.title))
            continue;
        TitleProposal proposal {volume, std::nullopt, false};
        bool alreadyThere = false;
        for (const auto& row : rows) {
            if (!row.position || taken.contains(row.entryId))
                continue;
            const bool samePosition = volume.ordinal && trimmed(*row.position) == trimmed(*volume.ordinal);
            const bool namesPosition = endsWithPosition(volume.title, *row.position);
            if (!isUnidentified(row)) {
                alreadyThere = alreadyThere || samePosition || namesPosition;
                continue;
            }
            if (samePosition || namesPosition) {
                proposal.entryId = row.entryId;
                proposal.chosen = true;
                break;
            }
        }
        if (!proposal.entryId && alreadyThere)
            continue;
        if (!proposal.entryId && volume.ordinal) {
            while (nextSlot < slots.size() && taken.contains(slots[nextSlot]))
                ++nextSlot;
            if (nextSlot < slots.size()) {
                proposal.entryId = slots[nextSlot++];
                proposal.chosen = true;
            }
        }
        if (proposal.entryId)
            taken.insert(*proposal.entryId);
        proposals.push_back(std::move(proposal));
    }
    return proposals;
}

} // namespace pinax::domain
