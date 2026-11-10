// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::ProgramCaptureState;

constexpr std::array<Instruction, 10> ExitHandlerProgram{{
    {{0x0000734e, 0, 0, 0x000fe400}},
    {{0x00037802, 0x70, 0x00000f00, 0x000fc800}},
    {{0x00007210, 3, 0x07f1e0ff, 0x000fc800}},
    {{0x01017210, 0xff, 0x007fe4ff, 0x000fc800}},
    {{0x00007357, 0, 0, 0x0001e400}},
    {{0x0000894d, 0, 0x03800000, 0x021fea00}},
    {{0x0000794d, 0, 0x03800000, 0x003fea00}},
    {{0x00ff7324, 0xff, 0, 0x0001e200}},
    {{0x00007957, 0, 0, 0x000fe200}},
    {{0x0000794d, 0, 0x03800000, 0x001fea00}},
}};

constexpr auto BananzaExitHandlerProgram = [] {
    std::array<Instruction, 0x740 / 16> code{};
    code.fill(Instruction{{0x00007918, 0, 0, 0x000fc000}});
    std::copy_n(ExitHandlerProgram.begin(), 5, code.begin());
    code[1].words[1] = 0x710;
    code[2].words[3] = 0x000fca00;
    code[3] = Instruction{{0xff017224, 0xff, 0x000e0401, 0x000fc800}};
    code[0x6e0 / 16] = ExitHandlerProgram[6];
    code[0x6f0 / 16] = Instruction{{0xff007322, 0x0003a0ff, 0, 0x0001e400}};
    code[0x700 / 16] = ExitHandlerProgram[6];
    std::copy_n(ExitHandlerProgram.begin() + 7, 3, code.begin() + 0x710 / 16);
    return code;
}();

TEST_CASE("Ampere IMAD exit capture includes the complete Bananza handler",
          "[video_core][ampere]") {
    ProgramCaptureState capture;
    for (u32 index = 0; index < BananzaExitHandlerProgram.size(); ++index) {
        const auto expected = index + 1 == BananzaExitHandlerProgram.size()
                                  ? ProgramCaptureState::Result::Complete
                                  : ProgramCaptureState::Result::Continue;
        REQUIRE(capture.Observe(BananzaExitHandlerProgram[index], index) == expected);
    }
    REQUIRE(sizeof(BananzaExitHandlerProgram) == 0x740);
}

TEST_CASE("Ampere IMAD exit capture requires exact carry fields", "[video_core][ampere]") {
    for (u32 word = 0; word < 3; ++word) {
        for (u32 bit = 0; bit < 32; ++bit) {
            auto code = BananzaExitHandlerProgram;
            code[3].words[word] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index <= 0x6e0 / 16; ++index) {
                const auto expected = index == 0x6e0 / 16
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere IMAD exit capture bounds the handler destination", "[video_core][ampere]") {
    for (const u32 target : {0U, 0x40U, 0x711U, ProgramCaptureState::MaximumInstructions * 16}) {
        auto code = BananzaExitHandlerProgram;
        code[1].words[1] = target;
        ProgramCaptureState capture;
        for (u32 index = 0; index < 4; ++index)
            REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
        REQUIRE(capture.Observe(code[4], 4) == ProgramCaptureState::Result::InvalidTarget);
    }
}

TEST_CASE("Ampere IMAD exit capture validates extended instruction modes",
          "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 30U, 31U}) {
            auto code = BananzaExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index <= 0x6e0 / 16; ++index) {
                const auto expected = index == 0x6e0 / 16
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere IMAD exit capture ignores scheduling fields", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit = 9; bit < 30; ++bit) {
            auto code = BananzaExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index < code.size(); ++index) {
                const auto expected = index + 1 == code.size()
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere IMAD exit capture does not bypass unsupported lowering",
          "[video_core][ampere]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(BananzaExitHandlerProgram);
    REQUIRE_FALSE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.empty());
    REQUIRE(lowered.error_offset == 0);
}

TEST_CASE("Ampere capture includes the static geometry exit handler", "[video_core][ampere]") {
    ProgramCaptureState capture;
    for (u32 index = 0; index < ExitHandlerProgram.size(); ++index) {
        const auto expected = index + 1 == ExitHandlerProgram.size()
                                  ? ProgramCaptureState::Result::Complete
                                  : ProgramCaptureState::Result::Continue;
        REQUIRE(capture.Observe(ExitHandlerProgram[index], index) == expected);
    }
}

TEST_CASE("Ampere capture bounds the static exit destination", "[video_core][ampere]") {
    for (const u32 target : {0U, 0x40U, 0x71U, ProgramCaptureState::MaximumInstructions * 16}) {
        auto code = ExitHandlerProgram;
        code[1].words[1] = target;
        ProgramCaptureState capture;
        for (u32 index = 0; index < 4; ++index)
            REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
        REQUIRE(capture.Observe(code[4], 4) == ProgramCaptureState::Result::InvalidTarget);
    }
}

TEST_CASE("Ampere capture does not guess other exit setup forms", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        auto code = ExitHandlerProgram;
        code[setup].words[0] ^= 1U << 12;
        ProgramCaptureState capture;
        for (u32 index = 0; index < 6; ++index)
            REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
        REQUIRE(capture.Observe(code[6], 6) == ProgramCaptureState::Result::Complete);
    }
}

TEST_CASE("Ampere static exit capture does not bypass unsupported lowering", "[video_core][ampere]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(ExitHandlerProgram);
    REQUIRE_FALSE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.empty());
    REQUIRE(lowered.error_offset == 0);
}

TEST_CASE("Ampere static exit capture validates extended instruction modes", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 30U, 31U}) {
            auto code = ExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index < 6; ++index)
                REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
            REQUIRE(capture.Observe(code[6], 6) == ProgramCaptureState::Result::Complete);
        }
    }
}

TEST_CASE("Ampere static exit capture ignores scheduling fields", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit = 9; bit < 30; ++bit) {
            auto code = ExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index < code.size(); ++index) {
                const auto expected = index + 1 == code.size()
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere geometry loop transfer is bounded to a nonreturning call pattern",
          "[video_core][ampere]") {
    const std::array<Instruction, 4> code{{
        {{0x00003944, 16, 0x03c00000, 0}},
        {{0x00007947, 0xffffffe0, 0x0383ffff, 0}},
        {{0x0000794d, 0, 0x03800000, 0}},
        {{0x0000794d, 0, 0x03800000, 0}},
    }};
    REQUIRE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(code)));
    auto returning = code;
    returning[2].words[0] = 0x00007950;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(returning)));
    auto conditional_branch = code;
    conditional_branch[1].words[0] = 0x00001947;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(conditional_branch)));
    auto extra_condition = code;
    extra_condition[0].words[2] ^= 1U << 24;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(extra_condition)));
    for (u32 instruction = 0; instruction < 2; ++instruction) {
        for (u32 bit : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 30U, 31U}) {
            auto extended_mode = code;
            extended_mode[instruction].words[3] ^= 1U << bit;
            REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(extended_mode)));
        }
    }
}

constexpr std::array<Instruction, 8> TerminalFloatTailProgram{{
    {{0x00027802, 0x20, 0x00000f00, 0}},
    {{0x00007944, 0x50, 0x03c00000, 0}},
    {{0x00000944, 0x10, 0x03c00000, 0}},
    {{0x00007947, 0xffffffe0, 0x0383ffff, 0}},
    {{0xff007209, 0x21, 0x07810000, 0x000fe400}},
    {{0x01017809, 0x447a0000, 0x03810000, 0x000fe400}},
    {{0x0000794d, 0, 0x03800000, 0x000fec00}},
    {{0x02007950, 0xffffff80, 0x03c3ffff, 0}},
}};

// The same tail inside a called library function. A RET there can return past the NOINC, so the
// tail proof is required; in the main program above, the library RET never returns to it.
constexpr std::array<Instruction, 9> LibraryFloatTailProgram{{
    {{0x00027802, 0x20, 0x00000f00, 0}},
    {{0x00007944, 0x10, 0x03c00000, 0}},
    {{0x0000794d, 0, 0x03800000, 0x000fec00}},
    {{0x00000944, 0x10, 0x03c00000, 0}},
    {{0x00007947, 0xffffffe0, 0x0383ffff, 0}},
    {{0xff007209, 0x21, 0x07810000, 0x000fe400}},
    {{0x01017809, 0x447a0000, 0x03810000, 0x000fe400}},
    {{0x0000794d, 0, 0x03800000, 0x000fec00}},
    {{0x02007950, 0xffffff80, 0x03c3ffff, 0}},
}};

TEST_CASE("Ampere terminal minmax tails allow an unrelated linked return",
          "[video_core][ampere]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(TerminalFloatTailProgram);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(TerminalFloatTailProgram));
    REQUIRE(lowered.instructions.back() == 0xe32000000007000fULL);
}

TEST_CASE("Ampere terminal minmax proof rejects intervening control and unknown state",
          "[video_core][ampere]") {
    for (const Instruction intervening : {
             Instruction{{0x02007950, 0xffffffb0, 0x03c3ffff, 0}},
             Instruction{{0x00007944, 0, 0x03c00000, 0}},
             Instruction{{0x00007947, 0, 0x03800000, 0}},
             Instruction{{0x00007949, 0xffffffb0, 0x0383ffff, 0}},
             Instruction{{0x00007958, 0xffffffb0, 0x0b83ffff, 0}},
             Instruction{{0x0000734e, 0, 0, 0}},
             Instruction{{0x00007357, 0, 0, 0}},
             Instruction{{0x00007957, 0, 0, 0}},
             Instruction{{0x00007fff, 0, 0, 0}},
         }) {
        auto code = LibraryFloatTailProgram;
        code[5] = intervening;
        const auto lowered = Shader::Ampere::LowerToMaxwell(code);
        REQUIRE_FALSE(static_cast<bool>(lowered));
        REQUIRE(lowered.error_offset == 0x30);
    }
}

TEST_CASE("Ampere terminal minmax proof requires an exact unconditional exit",
          "[video_core][ampere]") {
    for (const Instruction exit : {
             Instruction{{0x0000194d, 0, 0x03800000, 0}},
             Instruction{{0x0000f94d, 0, 0x03800000, 0}},
             Instruction{{0x0000794d, 0, 0x03900000, 0}},
             Instruction{{0x0000794d, 0, 0x03000000, 0}},
             Instruction{{0x0000794d, 1, 0x03800000, 0}},
         }) {
        auto code = TerminalFloatTailProgram;
        code[6] = exit;
        const auto lowered = Shader::Ampere::LowerToMaxwell(code);
        REQUIRE_FALSE(static_cast<bool>(lowered));
        REQUIRE(lowered.error_offset == 0x20);
    }
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(
        std::span<const Instruction>{TerminalFloatTailProgram}.first(4))));
    auto distant_target = TerminalFloatTailProgram;
    distant_target[2].words[1] = 0x1000;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(distant_target)));
}

TEST_CASE("Ampere terminal minmax proof rejects extended instruction modes",
          "[video_core][ampere]") {
    REQUIRE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(LibraryFloatTailProgram)));
    for (u32 instruction = 3; instruction <= 7; ++instruction) {
        for (u32 bit : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 30U, 31U}) {
            auto code = LibraryFloatTailProgram;
            code[instruction].words[3] ^= 1U << bit;
            REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(code)));
        }
    }
    auto predicated = LibraryFloatTailProgram;
    predicated[5].words[0] ^= 1U << 12;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(predicated)));
    auto unknown_float_mode = LibraryFloatTailProgram;
    unknown_float_mode[5].words[2] ^= 1U << 10;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(unknown_float_mode)));
    auto invalid_register_form = LibraryFloatTailProgram;
    invalid_register_form[5].words[1] |= 1U << 8;
    REQUIRE_FALSE(static_cast<bool>(Shader::Ampere::LowerToMaxwell(invalid_register_form)));
}
TEST_CASE("Ampere capture falls back to the closing self-branch before an invalid branch",
          "[video_core][ampere]") {
    // NOP, BRA to itself (PT, UPT), then data that decodes as a branch before the program start.
    constexpr std::array<Instruction, 3> code{{
        {{0x00007918, 0, 0, 0x000fc000}},
        {{0x00007947, 0xfffffff0, 0x03bfffff, 0x000fc000}},
        {{0x00007947, 0xffffff00, 0x03bfffff, 0x000fc000}},
    }};
    ProgramCaptureState capture;
    REQUIRE(capture.Observe(code[0], 0) == ProgramCaptureState::Result::Continue);
    REQUIRE(capture.Observe(code[1], 1) == ProgramCaptureState::Result::Continue);
    REQUIRE(capture.Observe(code[2], 2) == ProgramCaptureState::Result::InvalidTarget);
    REQUIRE(capture.FallbackEnd() == 2);

    // A self-branch before a forward branch target is no end.
    auto guarded = code;
    guarded[0] = Instruction{{0x00007947, 0x00000020, 0x03800000, 0x000fc000}}; // BRA +0x20
    ProgramCaptureState forward;
    for (u32 index = 0; index < 2; ++index) {
        REQUIRE(forward.Observe(guarded[index], index) == ProgramCaptureState::Result::Continue);
    }
    REQUIRE(forward.Observe(guarded[2], 2) == ProgramCaptureState::Result::InvalidTarget);
    REQUIRE(forward.FallbackEnd() == 0);
}

TEST_CASE("Ampere capture ends at the library RET behind an unpredicated CALL.REL.NOINC",
          "[video_core][ampere]") {
    // Not earlier (at the library's own EXIT) and not later (in the next program in memory, where
    // Orbitals' bindless handle proof then refused valid vertex shaders).
    const auto end = [](std::span<const Instruction> code) {
        ProgramCaptureState capture;
        for (u32 index = 0; index < code.size(); ++index) {
            if (capture.Observe(code[index], index) == ProgramCaptureState::Result::Complete)
                return index + 1;
        }
        return 0U;
    };
    REQUIRE(end(TerminalFloatTailProgram) == TerminalFloatTailProgram.size());
    REQUIRE(end(LibraryFloatTailProgram) == LibraryFloatTailProgram.size());
    // The predicated form stays a jump: its target is no function, so the EXIT there ends it.
    constexpr std::array<Instruction, 3> loop{{
        {{0x00003944, 16, 0x03c00000, 0}},
        {{0x00007947, 0xffffffe0, 0x0383ffff, 0}},
        {{0x0000794d, 0, 0x03800000, 0}},
    }};
    REQUIRE(end(loop) == loop.size());
    // Without NOINC it is a call whatever its predicate: the EXIT then only ends the main program.
    auto call = loop;
    call[0].words[2] = 0x03800000;
    REQUIRE(end(call) == 0);
}

} // namespace
