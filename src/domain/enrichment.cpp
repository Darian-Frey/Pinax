#include "domain/enrichment.h"

#include <algorithm>
#include <cctype>

namespace pinax::domain {

EnrichmentPlan planEnrichment(const Book& book, const Candidate& candidate, bool ownersEdition,
    const std::string& fetchedAt)
{
    EnrichmentPlan plan;
    plan.book = book;
    Book& updated = plan.book;

    // AV-001: what the owner wrote stays.
    if (candidate.description && book.synopsisSource != Source::Manual) {
        updated.synopsis = candidate.description;
        updated.synopsisSource = candidate.source;
    }

    const auto year = candidate.firstPublishedYear ? candidate.firstPublishedYear : candidate.publishedYear;
    if (!updated.publishedYear && year)
        updated.publishedYear = year;

    // Typical figures — a work's median page count — are no edition's.
    if (ownersEdition && !candidate.editionFactsTypical) {
        if (!updated.publisher && candidate.publisher)
            updated.publisher = candidate.publisher;
        if (!updated.pageCount && candidate.pageCount)
            updated.pageCount = candidate.pageCount;
    }

    for (const auto& name : candidate.categories)
        plan.genres.push_back({name, candidate.source});
    if (candidate.filledFrom) {
        for (const auto& name : candidate.filledCategories)
            plan.genres.push_back({name, *candidate.filledFrom});
    }
    if (candidate.coverUrl && book.coverSource != Source::Manual)
        plan.coverUrl = candidate.coverUrl;

    if (book.metadataStatus != MetadataStatus::Manual)
        updated.metadataStatus = MetadataStatus::Matched;
    updated.metadataFetchedAt = fetchedAt;
    return plan;
}

namespace {

// Lower case, letters and digits only, single spaces, no leading article.
std::string comparable(const std::string& title)
{
    std::string words;
    for (const unsigned char c : title) {
        if (std::isalnum(c) || c >= 0x80)
            words.push_back(static_cast<char>(std::tolower(c)));
        else if (!words.empty() && words.back() != ' ')
            words.push_back(' ');
    }
    while (!words.empty() && words.back() == ' ')
        words.pop_back();
    for (const char* article : {"the ", "a ", "an "}) {
        const std::string prefix(article);
        if (words.rfind(prefix, 0) == 0) {
            words.erase(0, prefix.size());
            break;
        }
    }
    return words;
}

// The title before any bracketed or appended part: "Titan (NASA Trilogy)",
// "Foundation and Empire: Book 2", "Sunstorm - A Time Odyssey" -> the title.
std::string mainTitle(const std::string& title)
{
    std::size_t end = title.size();
    for (const char* mark : {"(", "[", ":", ";", "/", " - ", " \xE2\x80\x94 ", " \xE2\x80\x93 "}) {
        const auto at = title.find(mark);
        if (at != std::string::npos && at > 0)
            end = std::min(end, at);
    }
    return title.substr(0, end);
}

} // namespace

bool titlesAgree(const std::string& ownTitle, const std::string& providerTitle)
{
    const std::string own = comparable(ownTitle);
    const std::string theirs = comparable(providerTitle);
    if (own.empty() || theirs.empty())
        return false;
    return own == theirs || comparable(mainTitle(providerTitle)) == own
        || comparable(mainTitle(ownTitle)) == theirs;
}

} // namespace pinax::domain
