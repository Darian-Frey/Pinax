#include "domain/enums.h"

#include <array>
#include <utility>

namespace pinax::domain {

namespace {

template <typename Enum, std::size_t N>
using NameTable = std::array<std::pair<Enum, std::string_view>, N>;

constexpr NameTable<ReadStatus, 4> readStatusNames{{
    {ReadStatus::Unread, "unread"},
    {ReadStatus::Reading, "reading"},
    {ReadStatus::Read, "read"},
    {ReadStatus::Abandoned, "abandoned"},
}};

constexpr NameTable<Binding, 5> bindingNames{{
    {Binding::Paperback, "paperback"},
    {Binding::Hardback, "hardback"},
    {Binding::Omnibus, "omnibus"},
    {Binding::Boxset, "boxset"},
    {Binding::Other, "other"},
}};

constexpr NameTable<MetadataStatus, 4> metadataStatusNames{{
    {MetadataStatus::Unmatched, "unmatched"},
    {MetadataStatus::Matched, "matched"},
    {MetadataStatus::Manual, "manual"},
    {MetadataStatus::Failed, "failed"},
}};

constexpr NameTable<Source, 3> sourceNames{{
    {Source::GoogleBooks, "google_books"},
    {Source::OpenLibrary, "open_library"},
    {Source::Manual, "manual"},
}};

template <typename Enum, std::size_t N>
std::string_view nameOf(const NameTable<Enum, N>& table, Enum value)
{
    for (const auto& [entry, name] : table) {
        if (entry == value)
            return name;
    }
    return {};
}

template <typename Enum, std::size_t N>
std::optional<Enum> valueOf(const NameTable<Enum, N>& table, std::string_view text)
{
    for (const auto& [entry, name] : table) {
        if (name == text)
            return entry;
    }
    return std::nullopt;
}

} // namespace

std::string_view toString(ReadStatus value) { return nameOf(readStatusNames, value); }
std::string_view toString(Binding value) { return nameOf(bindingNames, value); }
std::string_view toString(MetadataStatus value) { return nameOf(metadataStatusNames, value); }
std::string_view toString(Source value) { return nameOf(sourceNames, value); }

std::optional<ReadStatus> readStatusFromString(std::string_view text)
{
    return valueOf(readStatusNames, text);
}

std::optional<Binding> bindingFromString(std::string_view text)
{
    return valueOf(bindingNames, text);
}

std::optional<MetadataStatus> metadataStatusFromString(std::string_view text)
{
    return valueOf(metadataStatusNames, text);
}

std::optional<Source> sourceFromString(std::string_view text)
{
    return valueOf(sourceNames, text);
}

} // namespace pinax::domain
