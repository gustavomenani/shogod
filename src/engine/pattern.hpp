#pragma once

// Signature scanner. Patterns use IDA syntax: "48 8B 05 ?? ?? ?? ?? 48 85 C0".
// Compilation happens once at init; find() is a plain byte scan over a module
// range with no allocation.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace shogod::pattern {

struct compiled {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask;  // 0xFF = must match, 0x00 = wildcard

    bool valid() const noexcept { return !bytes.empty() && bytes.size() == mask.size(); }
};

// Returns an invalid `compiled` when the text is malformed.
compiled compile(std::string_view ida_pattern);

const std::uint8_t* find(const std::uint8_t* start, std::size_t size, const compiled& pat) noexcept;
std::uintptr_t find(std::uintptr_t start, std::size_t size, const compiled& pat) noexcept;

// Reads the int32 at `instruction + displacement_offset` and resolves
// rip-relative: result = instruction + displacement_offset + 4 + disp.
// Returns 0 when the read would leave the mapped image (caller guards).
std::uintptr_t resolve_rel32(std::uintptr_t instruction, std::size_t displacement_offset) noexcept;

}  // namespace shogod::pattern
