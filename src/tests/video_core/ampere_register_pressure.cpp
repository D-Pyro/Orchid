// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Operand lists of the native register shadowing (register_pressure.h): a guest register at or
// above R216 aliases a uniform mirror once the uniform bank is in use, so every register field an
// opcode's lowering reads or writes has to be listed, with the number of registers it spans.

#include <algorithm>
#include <array>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::NativeRegisterCopy;
using Shader::Ampere::PrepareRegisterPressure;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
// MOV R0, UR0: puts the uniform bank in use, so R216 and up are uniform mirrors.
constexpr Instruction UniformMove{{0x00007c02, 0, 0x08000f00, 0}};
// USHF.R.U32 UR5, UR4, 20: rebuilds the uniform handle pair of the TEX/TXD with handle UR4.
constexpr Instruction HandleShift{{0x3f057899, 0x14, 0x08011604, 0}};

struct Field {
    u32 bit, count;
};

Instruction With(Instruction instruction, u32 bit, u32 value, u32 width = 8) {
    const u32 mask = ((1U << width) - 1) << (bit % 32);
    instruction.words[bit / 32] = (instruction.words[bit / 32] & ~mask) | (value << (bit % 32));
    return instruction;
}

// `program[at]` names registers at or above R216 in `fields`: each must move to a borrowed register
// below R180 with the same quartet offset, its guest values copied in before and back after, no
// other bit may change, and the program must lower to those copies around the lowering of the
// renamed instruction.
void CheckShadowed(std::vector<Instruction> program, size_t at, const std::vector<Field>& fields) {
    const Instruction instruction = program[at];
    CAPTURE(instruction.words);
    const auto pressure = PrepareRegisterPressure(instruction, 216, 180);
    REQUIRE(pressure.valid);
    std::array<u32, 4> masks{};
    bool shadowed = false;
    for (const auto [bit, count] : fields) {
        CAPTURE(bit);
        masks[bit / 32] |= 255U << (bit % 32);
        const u32 original = instruction.Bits(bit, 8), renamed = pressure.instruction.Bits(bit, 8);
        if (original < 216 || original == 255) {
            CHECK(renamed == original);
            continue;
        }
        shadowed = true;
        CHECK(renamed < 180);
        CHECK(renamed % 4 == original % 4);
        for (u32 i = 0; i < count; ++i) {
            CHECK(std::ranges::count(pressure.prefix,
                                     NativeRegisterCopy(0, renamed + i, 1, original + i)) == 1);
            CHECK(std::ranges::count(pressure.suffix,
                                     NativeRegisterCopy(1, original + i, 0, renamed + i)) == 1);
        }
    }
    REQUIRE(shadowed);
    for (u32 word = 0; word < 4; ++word)
        CHECK((pressure.instruction.words[word] & ~masks[word]) ==
              (instruction.words[word] & ~masks[word]));

    const auto lowered = LowerToMaxwell(program);
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    program[at] = pressure.instruction;
    const auto direct = LowerToMaxwell(program);
    INFO(direct.error);
    REQUIRE(static_cast<bool>(direct));
    const auto& words = lowered.instructions;
    const auto begin = words.begin() + lowered.source_instruction_offsets[at];
    const auto end = words.begin() + lowered.source_instruction_offsets[at + 1];
    const auto direct_begin = direct.instructions.begin() + direct.source_instruction_offsets[at];
    const auto direct_end = direct.instructions.begin() + direct.source_instruction_offsets[at + 1];
    REQUIRE(end - begin == static_cast<std::ptrdiff_t>(pressure.prefix.size() +
                                                       pressure.suffix.size()) +
                               (direct_end - direct_begin));
    CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(), begin));
    CHECK(std::equal(direct_begin, direct_end, begin + pressure.prefix.size()));
    CHECK(std::equal(pressure.suffix.begin(), pressure.suffix.end(),
                     end - static_cast<std::ptrdiff_t>(pressure.suffix.size())));
}

void CheckShadowed(const Instruction& instruction, const std::vector<Field>& fields) {
    CheckShadowed({UniformMove, instruction, Exit}, 1, fields);
}

// Pins how many registers a field spans: ending in R223 stays inside the R220 quartet, ending in
// R224 crosses into the next one and is refused.
void CheckSpan(const Instruction& instruction, u32 bit, u32 count) {
    CAPTURE(instruction.words, bit, count);
    CHECK(PrepareRegisterPressure(With(instruction, bit, 224 - count), 216, 180).valid);
    if (count > 1)
        CHECK_FALSE(PrepareRegisterPressure(With(instruction, bit, 225 - count), 216, 180).valid);
}
} // namespace

TEST_CASE("Ampere register shadowing covers IMAD with a uniform source",
          "[video_core][ampere][pressure]") {
    // IMAD R229, R224, UR10, R218 (0xc24) and IMAD R229, R224, R218, UR10 (0xe24, B in 64-71).
    CheckShadowed({{0xe0e57c24, 0x0000000a, 0x0f8e02da, 0}}, {{16, 1}, {24, 1}, {64, 1}});
    CheckShadowed({{0xe0e57e24, 0x0000000a, 0x0f8e02da, 0}}, {{16, 1}, {24, 1}, {64, 1}});
}

TEST_CASE("Ampere register shadowing covers R2UR, uniform FMUL and FFMA, IMNMX and LEA immediate",
          "[video_core][ampere][pressure]") {
    // R2UR UR4, R228: bits 16-23 name the uniform register.
    CheckShadowed({{0xe40473c2, 0, 0x000e0000, 0}}, {{24, 1}});
    // FMUL R219, R226, UR4.
    CheckShadowed({{0xe2db7c20, 0x04, 0x08410000, 0}}, {{16, 1}, {24, 1}});
    // FFMA R229, R224, UR5, R227.
    CheckShadowed({{0xe0e57c23, 0x05, 0x080100e3, 0}}, {{16, 1}, {24, 1}, {64, 1}});
    // IMNMX R217, R226, c[0x3][0x10] and IMNMX R217, R226, R220.
    CheckShadowed({{0xe2d97a17, 0x00c00400, 0x03800000, 0}}, {{16, 1}, {24, 1}});
    CheckShadowed({{0xe2d97217, 0xdc, 0x03800000, 0}}, {{16, 1}, {24, 1}, {32, 1}});
    // LEA R217, R226, 0x8000.
    CheckShadowed({{0xe2d97811, 0x00008000, 0x078e80ff, 0}}, {{16, 1}, {24, 1}, {64, 1}});
}
