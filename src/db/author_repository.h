#pragma once

#include "domain/author.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pinax::db {

class Connection;

// Reads and writes `author` rows. A name is one person (D-007).
class AuthorRepository {
public:
    explicit AuthorRepository(Connection& connection);

    std::optional<domain::Author> find(std::int64_t id);
    std::optional<domain::Author> findByName(const std::string& name);

    // The id of the author with exactly this name, creating them if absent
    // with a sort name from domain::makeSortName. An existing author's sort
    // name is never touched.
    std::int64_t findOrCreate(const std::string& name);

    // Deletes authors that no book credits and that carry no notes
    // (IMP-003). Returns how many went. Run inside the caller's transaction,
    // after whatever changed the credits.
    int removeUncredited();

    std::int64_t count();

private:
    Connection& connection_;
};

} // namespace pinax::db
