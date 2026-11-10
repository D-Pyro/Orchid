// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <iterator>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};

u32 Bits(u64 word, u32 offset, u32 count) {
    return static_cast<u32>((word >> offset) & ((u64{1} << count) - 1));
}
bool IsLdc(u64 word) {
    return (word >> 56) == 0xef;
}
// MOV R, c[bank][offset]: word offset in bits 20-33, bank in bits 34-38.
bool IsCbufMov(u64 word) {
    return (word & 0xfff8000000000000ULL) == 0x4c98000000000000ULL;
}

// Maxwell LDC's offset is signed 16-bit, so from 0x8000 on a 64-bit load has to become two
// 32-bit reads into `first_register` and the one after it.
void CheckPairLoad(const auto& lower, u32 low_offset, u32 first_register) {
    const auto low = lower(low_offset);
    INFO(low.error);
    REQUIRE(low);
    REQUIRE(std::ranges::count_if(low.instructions, IsLdc) == 1);
    REQUIRE(std::ranges::count_if(low.instructions, IsCbufMov) == 0);

    for (const u32 offset : {0x8000U, 0xfff8U}) {
        const auto high = lower(offset);
        INFO(high.error);
        REQUIRE(high);
        REQUIRE(std::ranges::count_if(high.instructions, IsLdc) == 0);
        std::vector<u64> loads;
        std::ranges::copy_if(high.instructions, std::back_inserter(loads), IsCbufMov);
        REQUIRE(loads.size() == 2);
        for (u32 half = 0; half < 2; ++half) {
            REQUIRE(Bits(loads[half], 0, 8) == first_register + half);
            REQUIRE(Bits(loads[half], 20, 14) == offset / 4 + half);
            REQUIRE(Bits(loads[half], 34, 5) == 2);
            // The MOV write mask: anything but all four bits is not a plain 32-bit load.
            REQUIRE(Bits(loads[half], 39, 4) == 0xf);
        }
    }
    // The pair would cross the end of the 16-bit offset range.
    REQUIRE_FALSE(lower(0xfffc));
}
} // namespace

TEST_CASE("Ampere wide multiply reads a constant addend past the signed LDC range",
          "[video_core][ampere][constant_load]") {
    // IMAD.WIDE R4, R2, c[2][offset]: the addend pair is read into the scratch pair R252/R253.
    CheckPairLoad(
        [](u32 offset) {
            return LowerToMaxwell(std::array{
                Instruction{{0x625U | (7U << 12) | (4U << 16) | (2U << 24),
                             (2U << 22) | (offset << 6), 0x078e0000U | 4U, 0}},
                Exit});
        },
        0x7ff8, 252);
}

TEST_CASE("Ampere 64-bit uniform constant loads past the signed LDC range use two reads",
          "[video_core][ampere][constant_load]") {
    // ULDC.64 UR4, c[2][offset]: UR4/UR5 live in R220/R221.
    CheckPairLoad(
        [](u32 offset) {
            return LowerToMaxwell(std::array{
                Instruction{{0x00047ab9, (2U << 22) | (offset << 6), 0xa00, 0}}, Exit});
        },
        0x168, 220);
}
