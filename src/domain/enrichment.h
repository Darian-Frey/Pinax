#pragma once

#include "domain/book.h"
#include "domain/candidate.h"

#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// What a confirmed candidate changes on a book (F-012 to F-015, AV-001,
// SPEC.md §3.5). Pure: the caller writes it.
struct EnrichmentPlan {
    Book book;                           // the book with the candidate's facts applied
    std::vector<std::string> categories; // to add as genres, verbatim (D-009)
    std::optional<std::string> coverUrl; // to fetch, unless the cover is the owner's
};

// The rules:
//   synopsis           written with its source, unless the owner wrote it
//   first-published    filled if empty
//   publisher, pages   filled if empty, and only from an ISBN lookup — a
//                      title search may describe another edition
//   ISBN               never written: a searched candidate's ISBN belongs to
//                      some edition, not necessarily the owner's copy
//   edition and condition notes, notes, title, read state, rating
//                      never touched
//   cover              offered for fetching unless the cover is manual
//   metadata status    matched, with the time given — unless the owner
//                      marked the book's metadata manual, which stays
EnrichmentPlan planEnrichment(const Book& book, const Candidate& candidate, bool fromIsbnLookup,
    const std::string& fetchedAt);

} // namespace pinax::domain
