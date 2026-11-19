// shogod :: brand strings — the only place the product name is assembled.
#include "brand.hpp"

#include <string>

namespace shogod::brand {

namespace {
const std::string k_tag = [] {
    std::string s;
    s.reserve(32);
    s.append(k_name);
    s.append(" v");
    s.append(k_version);
    return s;
}();
}  // namespace

std::string_view version_tag() noexcept { return k_tag; }

}  // namespace shogod::brand
