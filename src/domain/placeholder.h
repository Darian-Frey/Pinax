#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pinax::domain {

// A placeholder stands for a volume the series lacks but nobody has named:
// "Unidentified volume 3", "Later volumes — unidentified" (D-018). Its title
// is the only mark it carries, so this is the one place that reads it.
inline bool isPlaceholderTitle(std::string_view title)
{
    return title.starts_with("Unidentified volume") || title.starts_with("Later volumes — unidentified");
}

inline bool isPlaceholderTitle(const std::optional<std::string>& title)
{
    return title && isPlaceholderTitle(std::string_view(*title));
}

// "Later volumes — unidentified" stands for an unknown number of volumes,
// not one; "Unidentified volume 3" stands for one (F-030).
inline bool isOpenEndedPlaceholder(const std::optional<std::string>& title)
{
    return title && title->starts_with("Later volumes — unidentified");
}

} // namespace pinax::domain
