#pragma once

#include <string>
#include <string_view>

namespace pinax::domain {

// Filing form of a title: a leading English article moves to the end, so
// 'The Long Earth' files as 'Long Earth, The' (F-016).
std::string makeSortTitle(std::string_view title);

} // namespace pinax::domain
