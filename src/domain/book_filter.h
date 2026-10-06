#pragma once

#include "domain/enums.h"

#include <cstdint>

namespace pinax::domain {

// What the rail asks the middle panel to show (D-010, F-017): everything, the
// books in one read state, one series, or the volumes series are missing —
// all of them, or only where a series is one volume short (F-010).
struct BookFilter {
    enum class Kind { All, ReadState, Series, MissingVolumes, OneVolumeShort };

    Kind kind = Kind::All;
    ReadStatus readStatus = ReadStatus::Unread; // when kind is ReadState
    std::int64_t seriesId = 0;                  // when kind is Series

    bool operator==(const BookFilter&) const = default;
};

} // namespace pinax::domain
