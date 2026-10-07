#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pinax::domain {

// Providers spell names their own way: "Iain Banks" for the owner's
// "Iain M. Banks". These compare loosely — surname and first name, an
// initial standing for any name with its letter, middle names ignored,
// case and punctuation aside — for proposals the owner confirms (F-024),
// never for merging authors (D-007).

// The known name this provider name most likely means, if any.
std::optional<std::string> knownAuthor(const std::string& providerName, const std::vector<std::string>& known);

// Whether any name in one list is, loosely, a name in the other.
bool shareAnAuthor(const std::vector<std::string>& some, const std::vector<std::string>& others);

} // namespace pinax::domain
