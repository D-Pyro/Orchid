// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <span>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr std::array<Instruction, 2> Logged{{
    {{0x0c0d2c08, 0x00000007, 0x0d800000, 0x000fca00}},
    {{0x0e154c08, 0x00000007, 0x0e800000, 0x000fe200}},
}};

// The Maxwell SEL/MOV/LOP32I subset, following the production Maxwell frontend semantics.
// Distinct ordinary, uniform-mirror and shadow values expose accidental R/UR aliasing.
struct SelectMachine {
    std::array<std::array<u32, 256>, 3> registers{};
    std::array<bool, 7> predicates{};

    bool Predicate(u32 field) const {
        return ((field & 7) == 7 || predicates.at(field & 7)) != bool(field & 8);
    }
    u32 Read(u32 reg, u32 bank = 0) const { return reg == 255 ? 0 : registers.at(bank).at(reg); }
    void Run(std::span<const u64> code) {
        for (const u64 word : code) {
            const auto bits = [word](u32 bit, u32 count) {
                return static_cast<u32>((word >> bit) & ((u64{1} << count) - 1));
            };
            if (!Predicate(bits(16, 4))) continue;
            if (word == 0xe30000000007000fULL) return;
            const u32 dest = bits(0, 8), a = bits(8, 8), b = bits(20, 8);
            u32 value{};
            if ((word >> 48) == 0x5ca0) {
                value = Predicate(bits(39, 4)) ? Read(a) : Read(b);
            } else if ((word >> 48) == 0x5c98) {
                if (bits(44, 4) == 10 && bits(39, 4) == 0) {
                    registers.at(bits(8, 2)).at(dest) = Read(b, bits(10, 2));
                    continue;
                }
                REQUIRE(bits(39, 4) == 15);
                value = Read(b);
            } else if ((word >> 56) == 4) {
                const u32 immediate = bits(20, 32);
                REQUIRE((bits(53, 2) == 0 || bits(53, 2) == 2));
                value = bits(53, 2) == 0 ? Read(a) & immediate : Read(a) ^ immediate;
            } else {
                FAIL("Unexpected instruction in FSEL lowering");
            }
            if (dest != 255) registers[0][dest] = value;
        }
        FAIL("FSEL lowering did not reach EXIT");
    }
};

TEST_CASE("Kirby captured FSEL selects a uniform source under independent lane predicates",
          "[video_core][ampere][kirby][fsel]") {
    for (const auto& logged : Logged) {
        for (const bool invert_guard : {false, true}) {
            auto instruction = logged;
            if (invert_guard) instruction.words[0] ^= 1U << 15;
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit}, true);
            INFO(lowered.error << " at " << lowered.error_offset);
            REQUIRE(lowered);
            for (const bool guard : {false, true}) {
                for (const bool select : {false, true}) {
                    SelectMachine machine;
                    const u32 dst = instruction.Bits(16, 8), a = instruction.Bits(24, 8);
                    machine.registers[0][dst] = 0xdeadbeef;
                    machine.registers[0][a] = 0xffc12345; // Preserve a NaN payload bit-for-bit.
                    machine.registers[0][7] = 0x3f800000; // R7 is not UR7.
                    machine.registers[0][223] = 0x80000000; // UR7 mirror, negative zero.
                    machine.predicates[instruction.Bits(12, 3)] = guard;
                    machine.predicates[instruction.Bits(87, 3)] = select;
                    machine.Run(lowered.instructions);
                    const bool execute = guard != invert_guard;
                    CHECK(machine.registers[0][dst] ==
                          (execute ? (select ? 0x80000000U : 0xffc12345U) : 0xdeadbeefU));
                    CHECK(machine.registers[0][a] == 0xffc12345);
                    CHECK(machine.registers[0][223] == 0x80000000);
                    CHECK(machine.registers[0][7] == 0x3f800000);
                }
            }
        }
    }
}

TEST_CASE("Uniform FSEL retains floating absolute and negate modifiers before selection",
          "[video_core][ampere][kirby][fsel]") {
    for (u32 modifiers = 0; modifiers < 16; ++modifiers) {
        auto instruction = Logged[0];
        instruction.words[2] |= ((modifiers & 1) << 9) | (((modifiers >> 1) & 1) << 8);
        instruction.words[1] |= (((modifiers >> 2) & 1) << 30) |
                                (((modifiers >> 3) & 1) << 31);
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        for (const bool select : {false, true}) {
            SelectMachine machine;
            machine.predicates[2] = true;
            machine.predicates[3] = select;
            machine.registers[0][12] = 0xffc12345;
            machine.registers[0][223] = 0x80000000;
            const bool absolute = (modifiers >> (select ? 2 : 0)) & 1;
            const bool negate = (modifiers >> (select ? 3 : 1)) & 1;
            u32 expected = select ? 0x80000000U : 0xffc12345U;
            if (absolute) expected &= 0x7fffffff;
            if (negate) expected ^= 0x80000000;
            machine.Run(lowered.instructions);
            CHECK(machine.registers[0][13] == expected);
        }
    }
}

TEST_CASE("Uniform FSEL shadows ordinary high registers without changing the UR field",
          "[video_core][ampere][kirby][fsel]") {
    for (const u32 a : {215U, 216U, 217U}) {
        auto instruction = Logged[0];
        instruction.words[0] = (a << 24) | (216U << 16) | 0x2c08;
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        for (const bool select : {false, true}) {
            SelectMachine machine;
            machine.predicates[2] = true;
            machine.predicates[3] = select;
            machine.registers[0][216] = 0x11223344; // UR0 must survive an ordinary R216 write.
            machine.registers[0][217] = 0x55667788;
            machine.registers[0][223] = 0x80000000;
            machine.registers[a >= 216 ? 1 : 0][a] = 0x7fc01234;
            machine.Run(lowered.instructions);
            CHECK(machine.registers[1][216] == (select ? 0x80000000U : 0x7fc01234U));
            CHECK(machine.registers[0][216] == 0x11223344);
            CHECK(machine.registers[0][217] == 0x55667788);
            CHECK(machine.registers[0][223] == 0x80000000);
        }
    }
    for (const u32 uniform : {27U, 28U, 62U, 63U}) {
        auto instruction = Logged[0];
        instruction.words[1] = uniform;
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        CHECK(lowered);
    }
    auto invalid = Logged[0];
    invalid.words[1] = 64;
    CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
}
} // namespace
