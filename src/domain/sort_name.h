#pragma once

#include <string>
#include <string_view>

namespace pinax::domain {

// Filing form of a person's name: surname first. 'Iain M. Banks' becomes
// 'Banks, Iain M.'. A lower-weight particle before the surname stays with it,
// so 'Jon Del Arroz' becomes 'Del Arroz, Jon' and 'Ursula K. Le Guin'
// 'Le Guin, Ursula K.'. A single-word name is returned as given.
//
// A guess, made once when the author is first recorded; the stored sort name
// is editable and is never recomputed.
std::string makeSortName(std::string_view name);

} // namespace pinax::domain
