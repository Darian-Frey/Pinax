#pragma once

#include "domain/enums.h"

#include <cstdint>
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

private:
    Connection& connection_;
};

} // namespace pinax::db
