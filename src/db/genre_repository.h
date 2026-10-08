#pragma once

#include "domain/book_query.h"
#include "domain/enums.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pinax::db {

class Connection;

struct GenreLink {
    std::string name;
    domain::Source source;

    bool operator==(const GenreLink&) const = default;
};

// Genres as providers name them, verbatim (D-009, F-014), and which books
// carry them with whose say-so.
class GenreRepository {
public:
    explicit GenreRepository(Connection& connection);

    std::int64_t findOrCreate(const std::string& name);

    // Links the book to the genre unless already linked; an existing link
    // keeps its source, so a genre the owner added stays theirs.
    void addToBook(std::int64_t bookId, const std::string& name, domain::Source source);

    std::vector<GenreLink> forBook(std::int64_t bookId);

    // Every genre some book carries, by name, with how many books (F-017).
    std::vector<domain::FilterOption> withCounts();

    // Removes every link a provider made to a lending or list tag
    // (domain::isServiceSubject, IMP-008) — never one the owner made — and
    // then any genre nothing links to. Returns how many links went.
    int removeServiceSubjects();

    // Every book's genres by name, for grouping the list (F-018).
    std::map<std::int64_t, std::vector<std::string>> byBook();

private:
    Connection& connection_;
};

} // namespace pinax::db
