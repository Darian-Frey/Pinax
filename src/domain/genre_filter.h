#pragma once

#include <string>

namespace pinax::domain {

// Whether a provider's subject is about the library copy or a list, not the
// book (IMP-008): Open Library's lending tags — "Accessible book",
// "Protected DAISY", "OverDrive", "Large type books" — its list and award
// machine tags ("nyt:trade-fiction-paperback=2013-03-31"), and the like.
// Such subjects are never stored as genres. Compared case-blind; every other
// subject is kept verbatim (D-009).
bool isServiceSubject(const std::string& subject);

} // namespace pinax::domain
