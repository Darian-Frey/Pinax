#pragma once

#include "domain/workbook.h"

#include <optional>
#include <string>

namespace pinax::io {

// Writes the workbook as an .xlsx file with libxlsxwriter (F-022, D-025):
// one worksheet a sheet, its headers bold, frozen and filterable; numbers
// as numbers, text as text, an empty cell left empty; columns sized to
// their contents. Written to `<path>.partial` and renamed over `path` only
// once the library reports success, so a failure leaves any earlier file
// untouched. Returns why not, if not.
std::optional<std::string> writeWorkbook(const domain::Workbook& workbook, const std::string& path);

} // namespace pinax::io
