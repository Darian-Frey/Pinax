#pragma once

#include "domain/enums.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pinax::domain {

// A credit by name, before it is resolved to an author row.
struct NamedCredit {
    std::string name;
    CreditRole role = CreditRole::Author;

    bool operator==(const NamedCredit&) const = default;
};

class CreditTextError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// SPEC.md §1.1, the one notation for credits, shared by import and the edit
// form: credits joined by ' & ', in cover order, each optionally followed by
// its role in parentheses — 'Larry Niven & Jerry Pournelle',
// 'Mike Ashley (editor)'. Empty text is no credits. Throws CreditTextError
// for an empty name or an unrecognised role.
std::vector<NamedCredit> parseCredits(std::string_view text);

// The inverse: 'Mike Ashley (editor)', with '(author)' left off.
std::string formatCredits(const std::vector<NamedCredit>& credits);

} // namespace pinax::domain
