#pragma once

#include "domain/enums.h"

#include <cstdint>

namespace pinax::domain {

// What the rail asks the list to show (D-010, F-017): everything, the books
// in one read state, or the books in one series.
struct BookFilter {
    enum class Kind { All, ReadState, Series };

    Kind kind = Kind::All;
    ReadStatus readStatus = ReadStatus::Unread; // when kind is ReadState
    std::int64_t seriesId = 0;                  // when kind is Series

    bool operator==(const BookFilter&) const = default;
};

} // namespace pinax::domain
