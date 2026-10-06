#pragma once

#include "domain/enums.h"

#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// What a metadata provider says a book is (SPEC.md §3). Not yet the owner's
// book: a candidate is shown, confirmed or set aside, and only then written
// — through provenance rules that never overwrite a hand-entered field
// (AV-001, AV-010).
struct Candidate {
    Source source = Source::OpenLibrary;
    std::string providerKey;            // Open Library edition or work key; Google volume id
    std::optional<std::string> workKey; // Open Library work, where the synopsis lives

    std::string title;
    std::optional<std::string> subtitle;
    std::vector<std::string> authors;
    std::optional<std::string> publisher;
    std::optional<int> publishedYear;      // this edition
    std::optional<int> firstPublishedYear; // the work
    std::optional<int> pageCount;
    std::optional<std::string> isbn13;
    std::optional<std::string> isbn10;
    std::optional<std::string> description;
    std::vector<std::string> categories; // verbatim (D-009), from `source`
    std::optional<std::string> coverUrl;

    // A second provider that filled this candidate's gaps — publisher,
    // pages, years — for the same ISBN, and the categories it gave, which
    // are recorded with its name (D-022).
    std::optional<Source> filledFrom;
    std::vector<std::string> filledCategories;

    bool operator==(const Candidate&) const = default;
};

// The outcome of one lookup: candidates, possibly none, or why it failed.
struct LookupResult {
    std::vector<Candidate> candidates;
    std::optional<std::string> error;

    bool operator==(const LookupResult&) const = default;
};

} // namespace pinax::domain
