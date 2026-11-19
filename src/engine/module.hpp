#pragma once

// Loaded-module lookup for the process we are injected into. Base + size come
// from the PE headers, not from a hardcoded RVA table.

#include <cstdint>
#include <string_view>

namespace shogod::module {

struct info {
    std::uintptr_t base = 0;
    std::size_t size = 0;

    [[nodiscard]] bool valid() const noexcept { return base != 0 && size != 0; }
    [[nodiscard]] std::uintptr_t end() const noexcept { return base + size; }
};

info find(std::string_view name) noexcept;

}  // namespace shogod::module
