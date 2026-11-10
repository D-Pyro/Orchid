# Plan 004: 64-bit constant loads at offsets 0x8000 and above read the right constant-buffer bytes

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` — unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat a6597b1ef2..HEAD -- src/shader_recompiler/frontend/ampere/lower_to_maxwell.h src/tests/CMakeLists.txt`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition. This header changes often.

## Status

- **Priority**: P2
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: bug
- **Planned at**: commit `a6597b1ef2`, 2026-11-10

## Why this matters

Switch 2 shaders ("Ampere" instructions) are translated to the older "Maxwell" instruction form
by `LowerToMaxwell`. Two Ampere instructions that load a 64-bit value from a constant buffer at
a fixed offset are translated to the Maxwell instruction `LDC`. The Ampere offset is an unsigned
16-bit number (0 to 0xFFFF). The Maxwell `LDC` offset field is *signed* 16-bit. So an offset of
0x8000 or more becomes a negative offset, and the shader silently reads the wrong memory:
nothing is logged and the draw is not skipped, the game just gets wrong numbers (typically wrong
materials, bones or colours). The same problem was already fixed for 8- and 16-bit loads by
refusing them (commit `cea03e218c`). For 64-bit loads a correct translation exists: two 32-bit
loads with the Maxwell `MOV` constant-buffer form, whose offset field is unsigned. After this
plan those two instructions produce the two-load form when the offset is 0x8000 or more, and
tests pin it.

## Current state

File: `src/shader_recompiler/frontend/ampere/lower_to_maxwell.h` (about 3,900 lines). It contains
one function, `LowerToMaxwell`, which loops over the input instructions and `switch`es on the
opcode. An input instruction is four 32-bit words; `insn.Bits(offset, count)` reads a bit field
across them. `emit(word)` appends one 64-bit Maxwell instruction. `fail(index, reason)` discards
everything and returns an error (the caller then tries another lowering or skips the draw).

Helpers defined near the top of the loop body:

```cpp
// src/shader_recompiler/frontend/ampere/lower_to_maxwell.h:225
const auto bank = [&]() -> u32 {
    const u32 value = insn.Bits(54, 5);
    return value == 24 ? 17U : value;
};
const auto cbuf = [&]() -> u64 {
    return (u64{insn.Bits(38, 16) / 4} << 20) | (u64{bank()} << 34);
};
const auto valid_cbuf = [&] {
    return !insn.Bits(91, 1) && bank() < 18 &&
           (insn.Bits(38, 16) % 4) == 0;
};
```

`insn.Bits(38, 16)` is the byte offset into the constant buffer. `cbuf()` is the operand of the
Maxwell `MOV R, c[bank][offset]` form (opcode bits `0x4c98...`): offset divided by 4 in a 14-bit
unsigned field at bit 20, bank at bit 34. That form can reach every 4-byte-aligned offset up to
0xFFFC.

**Site A: wide multiply-add with a constant-buffer addend (Ampere opcode `0x625`)**

```cpp
// src/shader_recompiler/frontend/ampere/lower_to_maxwell.h:403
case 0x225: case 0x625: case 0x825: {
    const u32 a = insn.Bits(24, 8);
    const u32 c = insn.Bits(64, 8);
    const u32 carry_output = insn.Bits(81, 3);
    if ((insn.words[2] & ~0x000e02ffU) != 0x07800000U || (dest & 1) || dest >= 246 ||
        (carry_output != 7 && insn.Bits(73, 1)) ||
        (opcode == 0x225 && (insn.words[1] & ~0xffU)) ||
        (opcode == 0x625 && (!valid_cbuf() || insn.Bits(59, 5))) ||
        (opcode != 0x625 && c != 255 && ((c & 1) || c >= 246)))
        return fail(index, "native wide multiply mode");
    const u32 b = opcode == 0x625 ? c : opcode == 0x825 ? 248U : insn.Bits(32, 8);
    if (opcode == 0x825) mov_imm(b, insn.words[1]);
    multiply_high(a, b, insn.Bits(73, 1));
    multiply_low(a, b, 255, false, 247);
    if (opcode == 0x625) {
        emit(0xef90000000000000ULL | predicate | 252 | (u64{255} << 8) |
             (u64{insn.Bits(38, 16)} << 20) | (u64{bank()} << 36) | (u64{5} << 48));
    } else {
```

The `emit(0xef90...)` is a Maxwell `LDC` of size 5 (64-bit) into registers 252 and 253, with the
raw 16-bit offset at bit 20.

**Site B: uniform constant load, direct form (Ampere opcode `0xab9`), 64-bit width**

```cpp
// src/shader_recompiler/frontend/ampere/lower_to_maxwell.h:952
case 0xab9: case 0xabb: { // ULDC U32/U64, direct or indexed.
    const u32 uniform_dest = insn.Bits(16, 8), index_reg = insn.Bits(24, 8);
    const u32 width = insn.Bits(73, 3);
    const u32 target = uniform_register(uniform_dest);
    const u32 index_target = opcode == 0xabb ? uniform_register(index_reg) : 255U;
    // Widths as Maxwell LDC's: U8/S8/U16/S16 (0-3, Star Fox's blit reads a U16), 32, 64.
    const u32 align = width < 2 ? 1 : width < 4 ? 2 : 4;
    // Maxwell LDC's offset is signed 16-bit, so a direct subword load past 0x7fff can't be encoded.
    if (target == ~0U || target == 255 || width > 5 ||
        (opcode == 0xab9 && width < 4 && insn.Bits(38, 16) >= 0x8000) ||
        (width == 5 && ((uniform_dest & 1) || uniform_register(uniform_dest + 1) != target + 1)) ||
        // Indexed loads align index + offset; the constant offset alone can be unaligned.
        bank() >= 18 || (opcode == 0xab9 && insn.Bits(38, 16) % align) || insn.Bits(62, 2) ||
        (insn.words[2] & ~0xe00U) != (opcode == 0xab9 ? 0U : 0x08000000U) ||
        index_target == ~0U)
        return fail(index, "native uniform constant load mode");
    if (opcode == 0xab9 && width == 4) {
        emit(0x4c98000000000000ULL | predicate | target | (u64{15} << 39) | cbuf());
    } else {
        emit(0xef90000000000000ULL | predicate | target |
             (u64{index_target} << 8) | (u64{insn.Bits(38, 16)} << 20) |
             (u64{bank()} << 36) | (u64{width} << 48));
    }
    break;
}
```

Width 4 (32-bit, direct) already uses the `MOV` form and is correct for all offsets. Width 5
(64-bit, direct, opcode `0xab9`) goes to the `else` branch and has the bug. Widths 0-3 above
0x7fff are refused by the existing guard.

Why `LDC` is signed, for your understanding (do not change these files):

```cpp
// src/shader_recompiler/frontend/maxwell/translate/impl/load_constant.h:32
BitField<20, 16, s64> offset;
// src/shader_recompiler/frontend/maxwell/translate/impl/load_constant.cpp
const IR::U32 imm{ir.Imm32(static_cast<s32>(ldc.offset))};
```

and the `MOV` constant-buffer operand is unsigned:

```cpp
// src/shader_recompiler/frontend/maxwell/translate/impl/impl.cpp:127
BitField<20, 14, u64> offset;
BitField<34, 5, u64> binding;
```

Tests live in `src/tests/video_core/`, Catch2. Every test `.cpp` is listed by hand in
`src/tests/CMakeLists.txt` (for example the line `    video_core/ampere_integer_multiply.cpp`).
The smallest example of the shape you need is `src/tests/video_core/ampere_uniform_immediate_shift.cpp`:

```cpp
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
...
TEST_CASE("Ampere captured uniform immediate high-word shifts lower", "[video_core][ampere]") {
    ...
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
```

`LowerToMaxwell` returns a `LoweredProgram`: it converts to `true` on success, `.instructions`
is the `std::vector<u64>` of emitted Maxwell words, `.error` is the reason text on failure.

Known-good encodings of the two instructions, taken from existing tests:

- Opcode `0x625` (from `ampere_integer_multiply.cpp:335`; guard 7 means "always", register 4 is
  the destination pair 4/5, register 2 the multiplicand, the second word holds bank 2 at bit 22
  and the byte offset at bit 6):
  `Instruction{{0x625U | (7U << 12) | (4U << 16) | (2U << 24), (2U << 22) | (16U << 6), 0x078e0000U | 4U, 0}}`
- Opcode `0xab9`, 64-bit, into uniform register 4 from bank 2, offset 0x168 (from
  `ampere_bindless_tex.cpp:485`): `Instruction{{0x00047ab9, 0x00805a00, 0xa00, 0}}`. The second
  word is `(2U << 22) | (0x168U << 6)`: bank at bit 22, byte offset at bit 6. The third word
  `0xa00` is width 5. Uniform register 4 maps to Maxwell register 220 (`uniform_register`:
  `216 + reg` for `reg < 22`), so the pair is registers 220 and 221.

Conventions: 4 spaces, lines up to about 100 columns, comments explain why in one or two lines.

## Commands you will need

Run all commands from the repository root.

| Purpose | Command | Expected on success |
|---|---|---|
| Configure a build with tests (once) | `./tools/build-local.sh --configure-only -DYUZU_TESTS=ON` | exit 0 |
| Build the tests | `cmake --build build-local --target tests --parallel 6` | exit 0 |
| Run the Ampere tests | `./build-local/bin/tests "[ampere]"` | `All tests passed` |
| Run the whole suite | `./build-local/bin/tests` | `All tests passed` |
| Mutation check (required by `AGENTS.md`) | `orchid-agent-test --build-dir build-local -- "[ampere]"` | exit 0 |

Notes: on the maintainer's PC add `-DCPM_SOURCE_CACHE=/home/leon/src/Eglantine/.cache/cpm` to the
configure command if that folder exists. Skip the configure if `build-local/CMakeCache.txt`
already has `YUZU_TESTS:BOOL=ON`. The header is included by about 27 test files, so the build
after an edit takes a while. `orchid-agent-test` is a local tool of the maintainer; if it does
not exist on your machine, say so in the report.

## Scope

**In scope** (the only files you should modify):

- `src/shader_recompiler/frontend/ampere/lower_to_maxwell.h` (the two sites above)
- `src/tests/video_core/ampere_constant_load_offsets.cpp` (create)
- `src/tests/CMakeLists.txt` (one added line)
- `plans/README.md` (status row)

**Out of scope** (do NOT touch):

- `src/shader_recompiler/frontend/ampere/lower_to_maxwell_upstream.h`: an intentional second
  lowerer kept as a fallback. It has the same `LDC` lines; leave them (see Maintenance notes).
- The indexed forms: opcode `0xabb` (same `case` as site B) and opcode `0xb82` (line 1007,
  "LDC indexed byte offset"). Their offset is added to a register and it is not known whether
  the hardware treats it as signed. Do not change them.
- Offsets below 0x8000: the emitted instructions must stay exactly as they are today. Shader
  cache keys depend on the output; changing it for existing shaders would invalidate caches.
- Everything under `src/shader_recompiler/frontend/maxwell/`.

## Git workflow

- One commit. Subject style of this repository, for example
  `ampere: end a native capture at the RET behind an unpredicated CALL.REL.NOINC`. Suggested
  subject: `ampere: load 64-bit constants past 0x7fff as two 32-bit reads`.
- The body explains the signed `LDC` offset and names the two opcodes.
- Do NOT push unless the operator told you to or you work under this repository's `AGENTS.md`.
  This is a graphics change: `AGENTS.md` requires the Mario Tennis Fever Free Play match gate
  before a push. That gate is run by the operator or main agent, not by this plan.

## Steps

### Step 1: Write the failing tests

Create `src/tests/video_core/ampere_constant_load_offsets.cpp`. Copy the licence header line(s)
from `src/tests/video_core/ampere_uniform_immediate_shift.cpp`. Content:

```cpp
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
```

Add the line `    video_core/ampere_constant_load_offsets.cpp` to the source list in
`src/tests/CMakeLists.txt`, directly after the line `    video_core/ampere_integer_multiply.cpp`.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[constant_load]"`
→ both test cases FAIL in the `0x8000` iteration at `count_if(..., IsLdc) == 0`. The first four
lines of `CheckPairLoad` (the `low` part) must pass in both. If the `low` part fails in either
test case (the instruction does not lower at all, or its small-offset output is not one `LDC`),
STOP and report the printed `low.error`.

### Step 2: Fix site A (opcode `0x625`)

In `lower_to_maxwell.h`, in `case 0x225: case 0x625: case 0x825:`:

1. Add one condition to the guard, directly after the line
   `(opcode == 0x625 && (!valid_cbuf() || insn.Bits(59, 5))) ||`:

```cpp
                // The addend pair must not cross the end of the 16-bit offset range.
                (opcode == 0x625 && insn.Bits(38, 16) > 0xfff8) ||
```

2. Replace the body of `if (opcode == 0x625) { ... }` (only the single `emit(0xef90...)`
   statement) with:

```cpp
            if (opcode == 0x625) {
                if (insn.Bits(38, 16) >= 0x8000) {
                    // Maxwell LDC's offset is signed 16-bit: read the pair as two 32-bit
                    // constants, whose offset field is unsigned.
                    emit(0x4c98000000000000ULL | predicate | 252 | (u64{15} << 39) | cbuf());
                    emit(0x4c98000000000000ULL | predicate | 253 | (u64{15} << 39) |
                         (cbuf() + (u64{1} << 20)));
                } else {
                    emit(0xef90000000000000ULL | predicate | 252 | (u64{255} << 8) |
                         (u64{insn.Bits(38, 16)} << 20) | (u64{bank()} << 36) | (u64{5} << 48));
                }
            } else {
```

The `else` branch that follows (for the other two opcodes) stays untouched. `cbuf() + (1 << 20)`
adds one to the 14-bit word offset; the guard from point 1 keeps it inside the field.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[constant_load]"`
→ the test case `"Ampere wide multiply reads a constant addend past the signed LDC range"` passes;
the other one still fails. Then `./build-local/bin/tests "[integer_multiply]"` → `All tests passed`.

### Step 3: Fix site B (opcode `0xab9`, width 5)

In `lower_to_maxwell.h`, in `case 0xab9: case 0xabb:`:

1. Add one condition to the guard, directly after the line that starts
   `(opcode == 0xab9 && width < 4 && insn.Bits(38, 16) >= 0x8000) ||`:

```cpp
                // A direct 64-bit load past 0x7fff becomes two 32-bit reads; the pair must not
                // cross the end of the 16-bit offset range.
                (opcode == 0xab9 && width == 5 && insn.Bits(38, 16) > 0xfff8) ||
```

2. Change the emit part to:

```cpp
            if (opcode == 0xab9 && width == 4) {
                emit(0x4c98000000000000ULL | predicate | target | (u64{15} << 39) | cbuf());
            } else if (opcode == 0xab9 && width == 5 && insn.Bits(38, 16) >= 0x8000) {
                emit(0x4c98000000000000ULL | predicate | target | (u64{15} << 39) | cbuf());
                emit(0x4c98000000000000ULL | predicate | (target + 1) | (u64{15} << 39) |
                     (cbuf() + (u64{1} << 20)));
            } else {
                emit(0xef90000000000000ULL | predicate | target |
                     (u64{index_target} << 8) | (u64{insn.Bits(38, 16)} << 20) |
                     (u64{bank()} << 36) | (u64{width} << 48));
            }
```

The existing guard already guarantees for width 5 that `target + 1` is the second register of
the pair, that the offset is a multiple of 4 and that `bank() < 18`.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[ampere]"`
→ `All tests passed`.

### Step 4: Whole suite and mutation check

**Verify**: `./build-local/bin/tests` → `All tests passed`.

**Verify**: `orchid-agent-test --build-dir build-local -- "[ampere]"` → exit 0. On exit 1 read
the report under `~/Orchid/orchid-issues/mutation/<run>/`, add the assertion that kills the
surviving mutant (the boundary values 0x7ff8, 0x8000, 0xfff8 and 0xfffc are the ones mutants
move), and run again. Never lower the threshold.

## Test plan

- New file `src/tests/video_core/ampere_constant_load_offsets.cpp` with two test cases, one per
  opcode. Each checks: below the boundary the output is still one `LDC`; at 0x8000 and 0xfff8 it
  is two `MOV` constant loads into the right register pair with the right word offsets and bank;
  0xfffc is refused.
- The existing tests in `ampere_integer_multiply.cpp` and `ampere_bindless_tex.cpp` keep the
  output for small offsets pinned and must pass unchanged.
- Structural pattern: `src/tests/video_core/ampere_uniform_immediate_shift.cpp`.
- Verification: `./build-local/bin/tests "[constant_load]"` → all pass, 2 test cases.

## Done criteria

ALL must hold:

- [ ] `./build-local/bin/tests "[ampere]"` prints `All tests passed`
- [ ] `./build-local/bin/tests` prints `All tests passed`
- [ ] `orchid-agent-test --build-dir build-local -- "[ampere]"` exits 0 (or the report states the tool is not installed)
- [ ] `grep -c "cbuf() + (u64{1} << 20)" src/shader_recompiler/frontend/ampere/lower_to_maxwell.h` prints `2`
- [ ] `git diff --stat` shows no change to `lower_to_maxwell_upstream.h`
- [ ] `./build-local/bin/tests "[constant_load]"` prints `All tests passed` with 2 test cases
- [ ] `git status --short` lists only `lower_to_maxwell.h`, the new test file, `src/tests/CMakeLists.txt` and `plans/README.md`. The `plans/` folder may show as untracked (`?? plans/`); that is expected.
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- Either site does not match its excerpt in "Current state" (this file changes almost daily).
- In step 1 the `low` part of either new test case fails before any production change.
- An existing `[ampere]` test fails after your change. The output for offsets below 0x8000 must
  be byte-for-byte what it was.
- You are tempted to change the indexed forms (`0xabb`, `0xb82`) or the upstream fallback file.

## Maintenance notes

- Indexed loads (`0xabb`, `0xb82`) still copy the raw 16-bit offset into the signed `LDC` field.
  If Ampere treats that offset as unsigned, an indexed load with a constant part of 0x8000 or
  more reads 0x10000 bytes too low. Deciding that needs a real shader that uses such a load (a
  shader dump plus a comparison against the console's picture); until then it is left alone.
- `lower_to_maxwell_upstream.h` is only used when this lowerer refuses a program. With this
  change the two instructions are no longer refused or mis-lowered here, so the fallback is not
  reached because of them.
- The 8- and 16-bit direct loads past 0x7fff are still refused (existing guard). They could be
  lowered as a 32-bit `MOV` read plus a bit-field extract if a game needs them.
- Before a push the Mario Tennis Fever match gate from `AGENTS.md` applies, because this changes
  shader output for affected programs. Programs without such loads produce identical output.
