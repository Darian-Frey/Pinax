#pragma once

#include <cstdint>
#include <string>

namespace pinax::domain {

// One person (D-007). A joint credit is several authors, never one combined
// record.
struct Author {
    std::int64_t id = 0;
    std::string name;     // display form: 'Alastair Reynolds'
    std::string sortName; // filing form:  'Reynolds, Alastair'

    bool operator==(const Author&) const = default;
};

} // namespace pinax::domain
