#pragma once

#include "domain/book.h"
#include "domain/candidate.h"
#include "domain/enums.h"

#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// What a confirmed candidate changes on a book (F-012 to F-015, AV-001,
// SPEC.md §3.5). Pure: the caller writes it.
struct EnrichmentPlan {
    Book book;                           // the book with the candidate's facts applied
    struct Genre {
        std::string name;
        Source source;
        bool operator==(const Genre&) const = default;
    };
    std::vector<Genre> genres;           // to add, verbatim (D-009), each with its provider;
                                         // never a lending or list tag (IMP-008)
    std::optional<std::string> coverUrl; // to fetch, unless the cover is the owner's
};

// The rules:
//   synopsis           written with its source, unless the owner wrote it
//   first-published    filled if empty
//   publisher, pages   filled if empty, and only from the owner's edition:
//                      an ISBN match, or a searched candidate the owner has
//                      confirmed as theirs (D-029) — a title search alone
//                      may describe another edition; never figures marked
//                      typical (a work's median page count)
//   ISBN               never written: a searched candidate's ISBN belongs to
//                      some edition, not necessarily the owner's copy
//   edition and condition notes, notes, title, read state, rating
//                      never touched
//   cover              offered for fetching unless the cover is manual
//   metadata status    matched, with the time given — unless the owner
//                      marked the book's metadata manual, which stays
EnrichmentPlan planEnrichment(const Book& book, const Candidate& candidate, bool ownersEdition,
    const std::string& fetchedAt);

// Whether a provider's title plausibly names the owner's book, for accepting
// an ISBN's single answer without asking (SPEC.md §3.4, AV-010). Compared
// case-blind, ignoring punctuation and a leading article. One may carry more
// only after a bracket, colon, slash or dash — "Titan (NASA Trilogy)",
// "Foundation and Empire: Book 2" — never more words: "Dune" does not agree
// with "The Dune Encyclopedia".
bool titlesAgree(const std::string& ownTitle, const std::string& providerTitle);

} // namespace pinax::domain
