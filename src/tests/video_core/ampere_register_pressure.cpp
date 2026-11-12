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

TEST_CASE("Ampere register shadowing covers FMNMX and FADD without a register B, F2F and IDP",
          "[video_core][ampere][pressure]") {
    // The immediates end in 0xe8: their low byte is no register and must not be renamed.
    for (const Instruction instruction : {
             Instruction{{0xe2d97809, 0x3f8000e8, 0x07810000, 0}}, // FMNMX R217, R226, imm
             Instruction{{0xe2d97a09, 0x00c00400, 0x07810000, 0}}, // FMNMX R217, R226, c[0x3][0x10]
             Instruction{{0xe2d97c09, 0x00000004, 0x0f810000, 0}}, // FMNMX R217, R226, UR4
             Instruction{{0xe2d97821, 0x3f8000e8, 0, 0}},          // FADD R217, R226, imm
             Instruction{{0xe2d97a21, 0x00c00400, 0, 0}},          // FADD R217, R226, c[0x3][0x10]
             Instruction{{0xe2d97e21, 0x00000004, 0x08000000, 0}}, // FADD R217, R226, UR4
         }) {
        CheckShadowed(instruction, {{16, 1}, {24, 1}});
    }
    // F2F.F16.F32 R225, R228.
    CheckShadowed({{0x00e17304, 0xe4, 0x00200800, 0}}, {{16, 1}, {32, 1}});
    // IDP.4A R225, R226, R232, R236.
    CheckShadowed({{0xe2e17226, 0xe8, 0x000000ec, 0}}, {{16, 1}, {24, 1}, {32, 1}, {64, 1}});
}

TEST_CASE("Ampere register shadowing covers P2R, uniform I2FP and indexed LDC",
          "[video_core][ampere][pressure]") {
    // P2R R221, PR, R224, 0x1.
    CheckShadowed({{0xe0dd7803, 1, 0, 0}}, {{16, 1}, {24, 1}});
    // I2FP.F32.U32 R225, UR11.
    CheckShadowed({{0x00e17c45, 0x0b, 0x08201400, 0}}, {{16, 1}});
    // LDC.64 R226, c[0x7][R232 + 0x10]: the destination is a pair; LDC.32 writes one register.
    constexpr Instruction load{{0xe8e27b82, 0x01c00400, 0x00000a00, 0}};
    CheckShadowed(load, {{16, 2}, {24, 1}});
    CheckSpan(With(load, 24, 8), 16, 2);
    CheckSpan(With(With(load, 24, 8), 73, 4, 3), 16, 1);
}

TEST_CASE("Ampere register shadowing covers SHF with a register, cbuf or uniform amount",
          "[video_core][ampere][pressure]") {
    // SHF.R.U32.HI R225, RZ, R220, R234 and SHF.L.U32 R225, R224, R220, RZ.
    CheckShadowed({{0xffe17219, 0xdc, 0x00011eea, 0}}, {{16, 1}, {24, 1}, {32, 1}, {64, 1}});
    CheckShadowed({{0xe0e17219, 0xdc, 0x000006ff, 0}}, {{16, 1}, {24, 1}, {32, 1}, {64, 1}});
    // SHF.L.W.U32 R225, R224, UR6 / c[0x3][0x10], RZ and SHF.R.U32.HI R225, RZ, UR6, R234.
    CheckShadowed({{0xe0e17c19, 0x06, 0x08000eff, 0}}, {{16, 1}, {24, 1}, {64, 1}});
    CheckShadowed({{0xe0e17a19, 0x00c00400, 0x00000eff, 0}}, {{16, 1}, {24, 1}, {64, 1}});
    CheckShadowed({{0xffe17c19, 0x06, 0x08011eea, 0}}, {{16, 1}, {24, 1}, {64, 1}});
}

TEST_CASE("Ampere register shadowing covers LOP3 with a cbuf or uniform B",
          "[video_core][ampere][pressure]") {
    // LOP3.LUT R217, R226, c[0x3][0x10] / UR4, R228, 0xc0.
    CheckShadowed({{0xe2d97a12, 0x00c00400, 0x078ec0e4, 0}}, {{16, 1}, {24, 1}, {64, 1}});
    CheckShadowed({{0xe2d97c12, 0x04, 0x0f8ec0e4, 0}}, {{16, 1}, {24, 1}, {64, 1}});
}

TEST_CASE("Ampere register shadowing covers ATOMG data and every TXD register",
          "[video_core][ampere][pressure]") {
    // ATOMG.E.ADD R225, [UR4], R240; and with only the data high, which bits 32-39 must catch.
    CheckShadowed({{0xffe179a8, 0xf0, 0x081ee144, 0}}, {{16, 1}, {32, 1}});
    CheckShadowed({{0xff0479a8, 0xf0, 0x081ee144, 0}}, {{16, 1}, {32, 1}});
    // TXD.2D R224/R225 and R218/R219, (x, y) in R232, gradients R240-R243.
    constexpr Instruction gradient{{0xe8e07b6c, 0x200000f0, 0x001e0fda, 0}};
    CheckShadowed(gradient, {{16, 2}, {24, 2}, {32, 4}, {64, 2}});
    // Only the gradients high: bits 32-39 must be checked for TXD too.
    CheckShadowed({{0x08047b6c, 0x200000f0, 0x001e03ff, 0}}, {{16, 2}, {24, 2}, {32, 4}});
    // TXD.ARRAY_2D: (x, y, layer) in R232-R234.
    CheckShadowed({{0xe8e07b6c, 0xa00000f0, 0x001e03ff, 0}}, {{16, 2}, {24, 3}, {32, 4}});
    // The same with the handle in UR4 (0xf6c).
    CheckShadowed({HandleShift, {{0xe8e07f6c, 0xa00004f0, 0x081e0fe2, 0}}, Exit}, 1,
                  {{16, 2}, {24, 3}, {32, 4}, {64, 2}});

    constexpr Instruction low{{0x08047b6c, 0x20000010, 0x001e0f0a, 0}};
    for (const u32 opcode : {0xb6cU, 0xf6cU}) {
        const auto base = With(low, 0, opcode, 12);
        CheckSpan(base, 16, 2);
        CheckSpan(base, 24, 2);
        CheckSpan(With(base, 61, 5, 3), 24, 3);
        CheckSpan(base, 32, 4);
        CheckSpan(base, 64, 2);
        CheckSpan(With(base, 72, 7, 4), 64, 1);
        CheckSpan(With(With(base, 72, 1, 4), 64, 255), 16, 1);
    }
}

TEST_CASE("Ampere register shadowing covers SHFL, SUATOM, SUST and TLD4",
          "[video_core][ampere][pressure]") {
    // SHFL R225, R226, R228, 0x1f.
    constexpr Instruction shuffle{{0xe2e17589, 0x001f00e4, 0x000e0000, 0}};
    CheckShadowed(shuffle, {{16, 1}, {24, 1}, {32, 1}});
    // SUATOM.D.2D R225, [R232], R228 and SUST.P.2D [R232], R228-R231 / SUST.BA.2D [R232], R228.
    CheckShadowed({{0xe8e17593, 0x600000e4, 0x001ee000, 0}}, {{16, 1}, {24, 2}, {32, 1}});
    CheckShadowed({{0xe8007b9b, 0x600000e4, 0x00100f00, 0}}, {{24, 2}, {32, 4}});
    CheckShadowed({{0xe8007b9d, 0x600000e4, 0x0010a900, 0}}, {{24, 2}, {32, 1}});
    // TLD4.2D R224/R225 and R218/R219 from R232, R228; then the uniform-handle TLD4 (0xf63).
    constexpr Instruction gather{{0xe8e07b63, 0x200000e4, 0x000e0fda, 0}};
    CheckShadowed(gather, {{16, 2}, {24, 1}, {32, 1}, {64, 2}});
    CheckShadowed({{0xe9e07f63, 0x30000aec, 0x081e0fda, 0}}, {{16, 2}, {24, 1}, {32, 1}, {64, 2}});
    // Only the register in bits 32-39 high: the shadowing check must look at it.
    for (const Instruction instruction : {
             Instruction{{0x08047589, 0x001f00e4, 0x000e0000, 0}},
             Instruction{{0x08047593, 0x600000e4, 0x001ee000, 0}},
             Instruction{{0x08007b9b, 0x600000e4, 0x00100f00, 0}},
             Instruction{{0x08007b9d, 0x600000e4, 0x0010a900, 0}},
             Instruction{{0x08047b63, 0x200000e4, 0x000e0f0a, 0}},
             Instruction{{0x1d0c7f63, 0x30000aec, 0x081e0f14, 0}},
         }) {
        CheckShadowed(instruction, {{16, 1}, {32, 1}});
    }

    // Spans: SUATOM/SUST.P coordinates by dimension, SUST.P data by channel, TLD4 by layout.
    for (const u32 opcode : {0x593U, 0xb9bU}) {
        const auto base = With(Instruction{{0x08047000, 0x60000010, 0x00100100, 0}}, 0, opcode, 12);
        for (const auto [dimension, count] : {std::array{0U, 1U}, std::array{1U, 1U},
                                              std::array{3U, 2U}, std::array{4U, 3U},
                                              std::array{5U, 3U}})
            CheckSpan(With(base, 61, dimension, 3), 24, count);
    }
    CheckSpan(Instruction{{0x08007b9b, 0x60000010, 0x00100500, 0}}, 32, 2);
    CheckSpan(Instruction{{0x08007b9d, 0x60000010, 0x0010a900, 0}}, 24, 2);
    constexpr Instruction low{{0x08047b63, 0x20000010, 0x000e0f0a, 0}};
    for (const u32 opcode : {0xb63U, 0xf63U}) {
        const auto base = With(low, 0, opcode, 12);
        CheckSpan(base, 16, 2);
        CheckSpan(base, 64, 2);
        CheckSpan(With(base, 72, 7, 4), 64, 1);
        CheckSpan(With(base, 64, 255), 16, 4);
        CheckSpan(base, 24, 1);
        CheckSpan(With(base, 61, 5, 3), 24, 2);
        CheckSpan(With(base, 76, 1, 2), 24, 2);
    }
    // .F16 packs two channels per register.
    const auto half = With(low, 79, 1, 1);
    CheckSpan(half, 16, 1);
    CheckSpan(half, 64, 1);
    CheckSpan(With(half, 64, 255), 16, 2);
    // Per-tap offsets are proven by register number: never renamed.
    CHECK_FALSE(PrepareRegisterPressure(With(gather, 76, 2, 2), 216, 180).valid);

    // These lowerings stage through R250 and up: a span reaching it stays refused.
    for (const Instruction instruction : {
             Instruction{{0x08047589, 0x001f0010, 0x000e0000, 0}},
             Instruction{{0x08047593, 0x60000010, 0x001ee000, 0}},
             Instruction{{0x08007b9b, 0x60000010, 0x00100100, 0}},
             Instruction{{0x08007b9d, 0x60000010, 0x0010a900, 0}},
             Instruction{{0x08047b63, 0x20000010, 0x000e0f0a, 0}},
             Instruction{{0x1d0c7f63, 0x30000a10, 0x081e0f14, 0}},
         }) {
        CAPTURE(instruction.words);
        CHECK(PrepareRegisterPressure(With(instruction, 32, 249), 216, 180).valid);
        CHECK_FALSE(PrepareRegisterPressure(With(instruction, 32, 250), 216, 180).valid);
    }
    const auto staged = LowerToMaxwell(std::array{UniformMove, With(shuffle, 32, 250), Exit});
    REQUIRE_FALSE(static_cast<bool>(staged));
    CHECK(staged.error == "native register shadowing mode");
    // Without the uniform bank a lane in R246 and up is lowering scratch.
    const auto scratch = LowerToMaxwell(std::array{With(shuffle, 32, 246), Exit});
    REQUIRE_FALSE(static_cast<bool>(scratch));
    CHECK(scratch.error == "native program uses lowering scratch registers");
}

TEST_CASE("Ampere register shadowing covers all three coordinates of a 3D SURED",
          "[video_core][ampere][pressure]") {
    // SURED.D.OR.3D [R220], R228: x, y, z in R220-R222.
    constexpr Instruction reduction{{0xdc00759f, 0xa0c002e4, 0x0310e000, 0}};
    CheckShadowed(reduction, {{24, 3}, {32, 1}});
    CheckSpan(With(reduction, 32, 1), 24, 3);
    CheckSpan(With(With(reduction, 32, 1), 61, 3, 3), 24, 2);
    // R218-R220 crosses a quartet: the third coordinate would read UR4's mirror.
    const auto crossing = LowerToMaxwell(std::array{UniformMove, With(reduction, 24, 218), Exit});
    REQUIRE_FALSE(static_cast<bool>(crossing));
    CHECK(crossing.error == "native register shadowing mode");
}

TEST_CASE("Ampere register shadowing covers TEX with a uniform handle",
          "[video_core][ampere][pressure]") {
    // TEX.LZ R224/R225 and R226/R227 from (R232, R236), handle UR4.
    CheckShadowed({HandleShift, {{0xe8e07f60, 0x300004ec, 0x0c9e0fe2, 0}}, Exit}, 1,
                  {{16, 2}, {24, 1}, {32, 1}, {64, 2}});
    // TEX.LL ARRAY_2D: (layer, x) in R232/R233, (y, lod) in R236/R237.
    CheckShadowed({HandleShift, {{0xe8e07f60, 0xb00004ec, 0x099e0fe2, 0}}, Exit}, 1,
                  {{16, 2}, {24, 2}, {32, 2}, {64, 2}});

    constexpr Instruction low{{0x08047f60, 0x30000410, 0x0c9e0f0a, 0}};
    CheckSpan(low, 16, 2);
    CheckSpan(low, 64, 2);
    CheckSpan(With(low, 72, 7, 4), 64, 1);
    CheckSpan(With(With(low, 72, 15, 4), 64, 255), 16, 2); // channels 3-4 are dropped
    CheckSpan(With(low, 72, 1, 4), 16, 1);
    CheckSpan(low, 24, 1);
    CheckSpan(low, 32, 1);
    const auto lod = With(low, 87, 3, 3);
    CheckSpan(lod, 24, 2);
    CheckSpan(lod, 32, 1);
    CheckSpan(With(low, 78, 1, 1), 24, 2);
    const auto volume = With(low, 61, 2, 3);
    CheckSpan(volume, 24, 2);
    CheckSpan(volume, 32, 1);
    CheckSpan(With(volume, 87, 2, 3), 32, 2);
}

TEST_CASE("Ampere IMAD negates a constant buffer addend", "[video_core][ampere]") {
    // IMAD R1, R2, R3, -c[0x3][0x10].
    constexpr Instruction multiply{{0x02017624, 0x80c00400, 0x078e0003, 0}};
    const auto lowered = LowerToMaxwell(std::array{multiply, Exit});
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    const auto& words = lowered.instructions;
    // The addend is loaded into R252, negated there (R252 = RZ - R252) and added by the first XMAD.
    REQUIRE(words.size() >= 3);
    CHECK(words[0] == (0x4c98000000070000ULL | 252 | (u64{15} << 39) | (u64{4} << 20) |
                       (u64{3} << 34)));
    CHECK(words[1] == (0x5c10000000070000ULL | 252 | (u64{255} << 8) | (u64{252} << 20) |
                       (u64{1} << 48)));
    CHECK(words[2] == (0x5b00000000070000ULL | 250 | (u64{2} << 8) | (u64{3} << 20) |
                       (u64{252} << 39)));
    // Without the negation the addend is used as loaded.
    const auto positive = LowerToMaxwell(std::array{With(multiply, 63, 0, 1), Exit});
    REQUIRE(static_cast<bool>(positive));
    CHECK(positive.instructions.size() == words.size() - 1);
    CHECK(positive.instructions[1] == words[2]);
    // The absolute value (bit 62) stays rejected.
    const auto absolute = LowerToMaxwell(std::array{With(multiply, 62, 1, 1), Exit});
    REQUIRE_FALSE(static_cast<bool>(absolute));
    CHECK(absolute.error == "native multiply wide addend mode");
}

TEST_CASE("Ampere HSETP2 with a constant buffer compares through a scratch register",
          "[video_core][ampere]") {
    // HSETP2.GT.AND P1, PT, RZ.H0_H0, c[0x7][0xfc].H1_H1, !P1 (Hyrule Warriors).
    constexpr Instruction compare{{0xff007634, 0x31c03f00, 0x04f24000, 0}};
    const auto lowered = LowerToMaxwell(std::array{compare, Exit});
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.size() == 3);
    CHECK(lowered.instructions[0] == (0x4c98000000070000ULL | 250 | (u64{15} << 39) |
                                      (u64{0xfc / 4} << 20) | (u64{7} << 34)));
    // Maxwell HSETP2 register form: Q = PT, P = P1, Ra = RZ, Rb = R250.H1_H1, GT, !P1.
    CHECK(lowered.instructions[1] ==
          (0x5d20000000070000ULL | 7 | (u64{1} << 3) | (u64{255} << 8) | (u64{250} << 20) |
           (u64{3} << 28) | (u64{4} << 35) | (u64{1} << 39) | (u64{1} << 42)));
    // B's absolute (62) and negate (63) bits are those of the register form.
    const auto modified = LowerToMaxwell(std::array{With(compare, 62, 3, 2), Exit});
    REQUIRE(static_cast<bool>(modified));
    CHECK(modified.instructions[1] == (lowered.instructions[1] | (u64{3} << 30)));
    const auto unsupported = LowerToMaxwell(std::array{With(compare, 64, 1, 1), Exit});
    REQUIRE_FALSE(static_cast<bool>(unsupported));
    CHECK(unsupported.error == "native half comparison mode");
    const auto bad_bank = LowerToMaxwell(std::array{With(compare, 54, 20, 5), Exit});
    REQUIRE_FALSE(static_cast<bool>(bad_bank));
    CHECK(bad_bank.error == "native constant buffer address");
}
