#include "domain/enrichment.h"

namespace pinax::domain {

EnrichmentPlan planEnrichment(const Book& book, const Candidate& candidate, bool fromIsbnLookup,
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

    if (fromIsbnLookup) {
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

} // namespace pinax::domain
