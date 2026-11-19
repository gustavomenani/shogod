#include "engine/pattern.hpp"

#include <cstring>

namespace shogod::pattern {
namespace {

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c == '?') return -1;
    return -2;
}

}  // namespace

compiled compile(std::string_view ida_pattern) {
    compiled out;
    std::string_view s = ida_pattern;

    while (!s.empty()) {
        const std::size_t space = s.find_first_of(" \t");
        const std::string_view token = s.substr(0, space);

        if (token.empty()) {
            if (space == std::string_view::npos) break;
            s.remove_prefix(space + 1);
            continue;
        }

        if (token.size() == 2) {
            const int hi = hex_value(token[0]);
            const int lo = hex_value(token[1]);
            if (hi == -2 || lo == -2) return {};
            if (hi == -1 || lo == -1) {
                out.bytes.push_back(0);
                out.mask.push_back(0);
            } else {
                out.bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
                out.mask.push_back(0xFF);
            }
        } else if (token == "?") {
            out.bytes.push_back(0);
            out.mask.push_back(0);
        } else {
            return {};
        }

        if (space == std::string_view::npos) break;
        s.remove_prefix(space + 1);
    }

    if (out.bytes.empty() || out.bytes.size() != out.mask.size()) return {};
    return out;
}

const std::uint8_t* find(const std::uint8_t* start, std::size_t size, const compiled& pat) noexcept {
    if (start == nullptr || !pat.valid() || pat.bytes.size() > size) return nullptr;

    const std::size_t last = size - pat.bytes.size();
    const std::size_t len = pat.bytes.size();
    const std::uint8_t* mask = pat.mask.data();
    const std::uint8_t* needle = pat.bytes.data();

    for (std::size_t i = 0; i <= last; ++i) {
        const std::uint8_t* hay = start + i;
        // Match on the first byte first; most candidates die immediately.
        if ((hay[0] & mask[0]) != (needle[0] & mask[0])) continue;

        std::size_t j = 1;
        for (; j < len; ++j) {
            if ((hay[j] & mask[j]) != (needle[j] & mask[j])) break;
        }
        if (j == len) return hay;
    }
    return nullptr;
}

std::uintptr_t find(std::uintptr_t start, std::size_t size, const compiled& pat) noexcept {
    return reinterpret_cast<std::uintptr_t>(
        find(reinterpret_cast<const std::uint8_t*>(start), size, pat));
}

std::uintptr_t resolve_rel32(std::uintptr_t instruction, std::size_t displacement_offset) noexcept {
    if (instruction == 0) return 0;
    std::int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(instruction + displacement_offset), sizeof disp);
    return instruction + displacement_offset + sizeof disp + static_cast<std::intptr_t>(disp);
}

}  // namespace shogod::pattern
