#pragma once

#include <string_view>

namespace pinax::domain {

// An ISO 8601 calendar date as precise as it is known: a year (2019), a
// month (2019-03) or a day (2019-03-14), with the month and day in range.
// Stored as written; such text sorts in date order (F-016).
bool isPartialIsoDate(std::string_view text);

} // namespace pinax::domain
