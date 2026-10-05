#pragma once

#include <optional>
#include <string_view>

namespace pinax::io {

// SPEC.md §1.2: the sort key the importer derives from a printed series
// position when the file gives none.
//
//   '6', '6.5', 'Broadcast 6.5'  -> that number
//   '1-4', 'Broadcast 1-2'       -> the first number
//   '3a', '3b'                   -> 3.1, 3.2 (0.1 per letter)
//   'novellas', 'companion', ''  -> nullopt, which sorts last
//
// The only place in Pinax that reads a number out of `position` (D-005,
// AV-006). It runs at import; the stored sort_position is never recomputed.
std::optional<double> deriveSortPosition(std::string_view position);

} // namespace pinax::io
