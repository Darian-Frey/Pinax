#pragma once

#include "domain/enums.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::domain {

// One physical copy (D-003). Mirrors the `book` table; dates are ISO 8601
// text exactly as stored.
struct Book {
    std::int64_t id = 0; // 0 until stored

    std::string title;
    std::string sortTitle; // empty: derived from title when stored
    std::optional<std::string> subtitle;

    ReadStatus readStatus = ReadStatus::Unread;
    int timesRead = 0;
    std::optional<std::string> dateStarted;
    std::optional<std::string> dateFinished;
    std::optional<int> rating; // 1-10; nullopt is unrated

    std::optional<std::string> synopsis;
    std::optional<Source> synopsisSource;
    std::optional<std::string> coverPath;
    std::optional<Source> coverSource;

    std::optional<std::string> isbn13;
    std::optional<std::string> isbn10;
    std::optional<std::string> publisher;
    std::optional<int> publishedYear;
    std::optional<int> pageCount;
    std::string language = "en";
    std::optional<Binding> binding;

    std::optional<std::string> editionNote;
    std::optional<std::string> conditionNote;
    std::optional<std::string> acquiredDate;
    std::optional<std::string> acquiredNote;

    MetadataStatus metadataStatus = MetadataStatus::Unmatched;
    std::optional<std::string> metadataFetchedAt;

    std::optional<std::string> notes;

    // Maintained by the database; ignored on write.
    std::string createdAt;
    std::string updatedAt;

    bool operator==(const Book&) const = default;
};

} // namespace pinax::domain
