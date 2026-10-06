#pragma once

#include <optional>
#include <string_view>

namespace pinax::domain {

enum class ReadStatus { Unread, Reading, Read, Abandoned };

enum class Binding { Paperback, Hardback, Omnibus, Boxset, Other };

enum class MetadataStatus { Unmatched, Matched, Manual, Failed };

// Who supplied a value: a provider, or the owner by hand (F-015). A `Manual`
// value is never overwritten by enrichment (AV-001).
// BritishLibrary appears only as a genre's source: the British Library
// supplies no synopsis or cover (D-022), and the book's source columns do
// not accept it.
enum class Source { GoogleBooks, OpenLibrary, BritishLibrary, Manual };

// What a credited person did for a book (`book_author.role`).
enum class CreditRole { Author, Editor, Translator, Illustrator };

// The string forms are the values the schema's CHECK constraints accept.
std::string_view toString(ReadStatus value);
std::string_view toString(Binding value);
std::string_view toString(MetadataStatus value);
std::string_view toString(Source value);
std::string_view toString(CreditRole value);

std::optional<ReadStatus> readStatusFromString(std::string_view text);
std::optional<Binding> bindingFromString(std::string_view text);
std::optional<MetadataStatus> metadataStatusFromString(std::string_view text);
std::optional<Source> sourceFromString(std::string_view text);
std::optional<CreditRole> creditRoleFromString(std::string_view text);

} // namespace pinax::domain
