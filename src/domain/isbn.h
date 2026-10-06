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

// The ISBN-13 of a valid, normalised ISBN-10: 978, its first nine digits and
// a check digit of its own.
std::string isbn10To13(std::string_view isbn10);

} // namespace pinax::domain
