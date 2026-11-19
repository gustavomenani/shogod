#include <gtest/gtest.h>

#include <cstring>

#include "engine/pattern.hpp"

using namespace shogod::pattern;

namespace {

struct blob {
    std::uint8_t data[64]{};
    std::size_t size = 0;
};

}  // namespace

TEST(pattern, compiles_ida_syntax) {
    const compiled p = compile("48 8B 05 ?? ?? ?? ?? 48 85 C0");
    ASSERT_TRUE(p.valid());
    EXPECT_EQ(p.bytes.size(), 10u);
    EXPECT_EQ(p.mask[3], 0);
    EXPECT_EQ(p.mask[4], 0);
    EXPECT_EQ(p.mask[0], 0xFF);
}

TEST(pattern, rejects_malformed) {
    EXPECT_FALSE(compile("48 8B 0").valid());
    EXPECT_FALSE(compile("ZZ 8B").valid());
    EXPECT_FALSE(compile("").valid());
}

TEST(pattern, finds_known_offset_with_wildcards) {
    blob b;
    const std::uint8_t payload[] = {0xDE, 0xAD, 0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0x48,
                                    0x85, 0xC0, 0xBE, 0xEF};
    std::memcpy(b.data, payload, sizeof payload);
    b.size = sizeof payload;

    const compiled p = compile("48 8B 05 ?? ?? ?? ?? 48 85 C0");
    const std::uint8_t* hit = find(b.data, b.size, p);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit - b.data, 2);
}

TEST(pattern, no_match_returns_null) {
    blob b;
    const std::uint8_t payload[] = {0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0x48, 0x85, 0xC1};
    std::memcpy(b.data, payload, sizeof payload);
    b.size = sizeof payload;

    EXPECT_EQ(find(b.data, b.size, compile("48 8B 05 ?? ?? ?? ?? 48 85 C0")), nullptr);
}

TEST(pattern, resolves_rip_relative) {
    // At address 0x1000: mov rax, [rip+disp] with disp = 0x20.
    // Instruction is 7 bytes; imm32 sits at +3. Target = 0x1000 + 3 + 4 + 0x20 = 0x1027.
    std::uint8_t code[8]{};
    code[3] = 0x20;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(code);
    EXPECT_EQ(resolve_rel32(base, 3), base + 3 + 4 + 0x20);
}
