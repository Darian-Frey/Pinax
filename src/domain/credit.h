#pragma once

#include "domain/enums.h"

#include <cstdint>

namespace pinax::domain {

// One `book_author` link: who, in what capacity, in what cover position.
struct Credit {
    std::int64_t authorId = 0;
    CreditRole role = CreditRole::Author;
    int ordinal = 0; // 0 = first-billed

    bool operator==(const Credit&) const = default;
};

} // namespace pinax::domain
