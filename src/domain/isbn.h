#pragma once

#include <string>
#include <string_view>

namespace pinax::domain {

// SPEC.md §2. Hyphens and spaces are stripped before validation; storage is
// digits only (and a final 'X' for ISBN-10).
std::string normaliseIsbn(std::string_view isbn);

// True for thirteen digits whose check digit is correct. Expects normalised
// input.
bool isValidIsbn13(std::string_view isbn);

// True for nine digits plus a correct check character, 'X' standing for 10.
// Expects normalised input.
bool isValidIsbn10(std::string_view isbn);

} // namespace pinax::domain
