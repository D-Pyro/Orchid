// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from zortos293's Lichii (62bc7538b, wired into the lowering in f130f0c50).
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <vector>
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader::Ampere {

struct RegisterPressure {
    Instruction instruction;
    std::vector<u64> prefix, suffix;
    bool valid = true;
};

// A Maxwell MOV with an empty write mask and 0b1010 in bits 44-47, a form Maxwell compilers never
// emit: copies between IR register banks of 256 (bank in bits 8-9 for the destination, 10-11 for
// the source). Bank 0 holds the Maxwell registers, bank 1 the shadowed guest registers and bank 2
// the saved values of borrowed registers.
inline u64 NativeRegisterCopy(u32 destination_bank, u32 destination, u32 source_bank, u32 source) {
    return 0x5c98000000070000ULL | (u64{10} << 44) | destination | (u64{destination_bank} << 8) |
           (u64{source_bank} << 10) | (u64{source} << 20);
}

// Guest registers at `reserved` and up alias the lowering's own state (uniform mirrors, scratch).
// For one instruction, each aligned quartet of them it touches is moved into a free quartet below
// `borrow_limit`: the prefix saves the borrowed registers to bank 2 and loads the guest values
// from bank 1, the suffix writes them back and restores the borrowed registers. Instructions whose
// register operands are not all known here stay invalid (rejected).
inline RegisterPressure PrepareRegisterPressure(const Instruction& input, u32 reserved,
                                                u32 borrow_limit) {
    RegisterPressure result{input, {}, {}};
    if (reserved > 246 || (reserved & 3) || borrow_limit < 4 || borrow_limit > reserved ||
        (borrow_limit & 3)) {
        result.valid = false;
        return result;
    }
    struct Operand {
        u32 bit, count;
    };
    std::vector<Operand> operands;
    const auto operand = [&](u32 bit, u32 count = 1) { operands.push_back({bit, count}); };
    const u32 opcode = input.Bits(0, 12);
    // The SHFL, SUATOM, SUST and TLD4 lowerings stage operands through R250-R254 (and the
    // predicate saves around them), so a shadowed span reaching R250 stays rejected for them.
    const bool staged_scratch = opcode == 0x589 || opcode == 0x593 || opcode == 0xb9b ||
                                opcode == 0xb9d || opcode == 0xb63 || opcode == 0xf63;
    switch (opcode) {
    case 0xa02:
    case 0xc3e:
    case 0x802:
    case 0xb08:
    case 0xd08:
    case 0x908:
    case 0x919:
        operand(16);
        break;
    case 0xa0c:
    case 0x804:
    case 0x80b:
    case 0x80c:
    case 0x3c2: // R2UR: bits 16-23 name the uniform register written
        operand(24);
        break;
    case 0x20c:
    case 0x20b:
        operand(24);
        operand(32);
        break;
    case 0x803: // P2R: Rd and the merge source Ra
    case 0x807:
    case 0x808:
    case 0xc08: // FSEL with a uniform B
    case 0x809: // FMNMX: B is an immediate, cbuf or uniform register, never a GPR
    case 0xa09:
    case 0xc09:
    case 0x923:
    case 0x820:
    case 0xa20:
    case 0xc20:
    case 0x421:
    case 0x821: // FADD: B is an immediate, cbuf or (0xe21) uniform register
    case 0xa21:
    case 0xe21:
        operand(16);
        operand(24);
        break;
    case 0x207:
    case 0x208:
    case 0x220:
    case 0x221:
    case 0x209:
        operand(16);
        operand(24);
        operand(32);
        break;
    case 0x805:
        operand(16, 2);
        break;
    case 0xc17:
    case 0x817:
    case 0xa17:
        operand(16);
        operand(24);
        break;
    case 0x217: // IMNMX register form: B in bits 32-39
    case 0x589: // SHFL: value in Ra, lane register at bit 32
        operand(16);
        operand(24);
        operand(32);
        break;
    case 0xa24:
    case 0xc24: // IMAD with a uniform B (0xc24) or uniform C (0xe24, B in bits 64-71)
    case 0xe24:
    case 0xa23:
    case 0xc23: // FFMA with a uniform B
    case 0x623:
    case 0x423:
        operand(16);
        operand(24);
        operand(64);
        break;
    case 0x621:
        operand(16);
        operand(24);
        break;
    case 0x321:
        operand(16, input.Bits(74, 2) + 1);
        operand(24);
        operand(32);
        break;
    case 0x322:
        operand(24);
        operand(32, input.Bits(74, 2) + 1);
        break;
    case 0x927:
        operand(24);
        operand(32);
        break;
    case 0x224:
    case 0x223:
        operand(16);
        operand(24);
        operand(32);
        operand(64);
        break;
    case 0x81a:
        operand(16);
        operand(24);
        break;
    case 0x308:
    case 0x9a8: // ATOMG: the address is a uniform register (bits 64-69), data at bit 32
    case 0x304: // F2F.F16.F32: the source is bits 32-39, bits 24-31 must be zero
    case 0x202:
    case 0x245:
    case 0x305:
    case 0x306:
    case 0x307:
        operand(16);
        operand(32);
        break;
    case 0xb05:
    case 0xd05:
    case 0xc45: // I2FP with a uniform source
        operand(16);
        break;
    case 0x593: // SUATOM: result, coordinates (1, 2 or 3 by dimension), data
        operand(16);
        operand(24, input.Bits(61, 3) >= 4 ? 3 : input.Bits(61, 3) == 3 ? 2 : 1);
        operand(32);
        break;
    case 0xb9b: // SUST.P: coordinates as SUATOM, one data register per stored channel
        operand(24, input.Bits(61, 3) >= 4 ? 3 : input.Bits(61, 3) == 3 ? 2 : 1);
        operand(32, static_cast<u32>(std::popcount(input.Bits(72, 4))));
        break;
    case 0xb9d: // SUST.BA 2D: (x, y) in Ra, Ra+1 and the data
        operand(24, 2);
        operand(32);
        break;
    case 0x59f: // SURED: 3D (dimension 5) has three coordinates
        operand(24, input.Bits(61, 3) == 5 ? 3 : 2);
        operand(32);
        break;
    case 0x98e:
        operand(24, input.Bits(72, 1) ? 2 : 1);
        operand(32, input.Bits(73, 3) == 2 || input.Bits(73, 3) == 5 ? 2 : 1);
        break;
    case 0x433:
    case 0x633:
        operand(16);
        operand(24);
        break;
    case 0x233:
        operand(16);
        operand(24);
        operand(32);
        break;
    case 0x810:
    case 0xa10:
    case 0xc10:
    case 0x812:
    case 0xa12: // LOP3.LUT with a cbuf or uniform B
    case 0xc12:
    case 0x824:
    case 0x823:
    case 0x831:
    case 0x424:
    case 0x624:
        operand(16);
        operand(24);
        operand(64);
        break;
    case 0x230:
    case 0x231:
        operand(16);
        operand(24);
        operand(32);
        operand(64);
        break;
    case 0x419:
    case 0x819:
    case 0xa19: // SHF with a cbuf or uniform shift amount
    case 0xc19:
        operand(16);
        operand(24);
        operand(64);
        break;
    case 0x210:
    case 0x211:
    case 0x212:
    case 0x219: // SHF, register shift amount in bits 32-39
    case 0x226: // IDP.4A: a, b and the accumulator
        operand(16);
        operand(24);
        operand(32);
        operand(64);
        break;
    case 0xa11:
    case 0x811: // LEA: B is the immediate in words[1]
        operand(16);
        operand(24);
        operand(64);
        break;
    case 0x225:
    case 0x227:
    case 0x625:
    case 0x825:
        operand(16, opcode == 0x227 ? 1 : 2);
        operand(24);
        if (opcode == 0x225 || opcode == 0x227) {
            operand(32);
            operand(64, 2);
        }
        if (opcode == 0x625)
            operand(64);
        if (opcode == 0x825)
            operand(64, 2);
        break;
    case 0x83b:
        operand(16, 1U << input.Bits(72, 2));
        operand(24);
        break;
    case 0x23c:
        operand(16, 2);
        operand(24, input.Bits(75, 1) ? 4 : 2);
        operand(32, input.Bits(75, 1) ? 2 : 1);
        operand(64, 2);
        break;
    case 0xb66: {
        // TLD, as case 0xb66 of the lowering reads it; counting a register too many only refuses more.
        if ((input.words[2] & ~0xffU) == 0x059e8700U) { // .F16 explicit fetch: (x, y), level, halves
            operand(16);
            operand(24, 2);
            operand(32);
            operand(64);
            break;
        }
        const u32 dimension = input.Bits(61, 3);
        const bool split = input.Bits(64, 8) != 255;
        operand(16, split ? 2U : static_cast<u32>(std::max(1, std::popcount(input.Bits(72, 4)))));
        operand(24, dimension == 2 || dimension == 5 || input.Bits(76, 1) ? 2 : 1);
        operand(32, dimension == 5 && input.Bits(87, 3) == 3 ? 2 : 1);
        if (split)
            operand(64, 2);
        break;
    }
    case 0x388:
    case 0x984:
    case 0x988:
    case 0x387:
    case 0x983:
    case 0x987: {
        const u32 width = input.Bits(73, 3), count = width == 6 ? 4 : width == 5 ? 2 : 1;
        operand(opcode == 0x984 || opcode == 0x983 ? 16 : 32, count);
        operand(24);
        break;
    }
    case 0xb82: // LDC indexed: Rd (a pair when 64-bit) and the index Ra
        operand(16, input.Bits(73, 3) == 5 ? 2 : 1);
        operand(24);
        break;
    case 0xb6c: // TXD bound or (0xf6c) with a uniform handle: Rd, Rd+1 and the second pair take
    case 0xf6c: { // the channels, (x, y[, layer]) in Ra, the four gradients in Rb.
        const u32 channels = static_cast<u32>(std::popcount(input.Bits(72, 4)));
        operand(16, std::min(channels, 2U));
        operand(24, input.Bits(61, 3) == 5 ? 3 : 2);
        operand(32, 4);
        if (channels > 2)
            operand(64, channels - 2);
        break;
    }
    case 0xb63:
    case 0xf63: { // TLD4: results in Rd.. and the second destination, Ra (a pair for ARRAY_2D or
                  // offsets), Rb.
        // Per-tap offsets are found by Rb's register number in earlier moves; a renamed Rb would
        // match the wrong ones.
        if (opcode == 0xb63 && input.Bits(76, 2) == 2) {
            result.valid = false;
            return result;
        }
        const u32 channels = static_cast<u32>(std::popcount(input.Bits(72, 4)));
        const bool split = input.Bits(64, 8) != 255, half = opcode == 0xb63 && input.Bits(79, 1);
        // .F16 packs two channels per register: Rd, then the second destination or Rd+1.
        operand(16, half ? (!split && channels > 2 ? 2U : 1U)
                         : split ? std::min(channels, 2U) : channels);
        operand(24, input.Bits(61, 3) == 5 || input.Bits(76, 2) ? 2 : 1);
        operand(32);
        if (split && channels > 2)
            operand(64, half ? 1 : channels - 2);
        break;
    }
    case 0xf60: { // TEX with a uniform handle, as case 0xf60 of the lowering packs it.
        const u32 channels = static_cast<u32>(std::popcount(input.Bits(72, 4)));
        const bool lod = input.Bits(87, 3) >= 2, planar = input.Bits(61, 3) == 1;
        operand(16, std::min(channels, 2U)); // without a second destination channels 3-4 drop
        operand(24, !planar || lod || input.Bits(78, 1) ? 2 : 1);
        operand(32, !planar && lod ? 2 : 1);
        if (channels > 2)
            operand(64, channels - 2);
        break;
    }
    case 0x381:
    case 0x386:
    case 0x981:
    case 0x986: {
        const u32 width = input.Bits(73, 3), count = width == 6 ? 4 : width == 5 ? 2 : 1;
        operand(opcode == 0x386 || opcode == 0x986 ? 32 : 16, count);
        operand(24, opcode == 0x381 || opcode == 0x386 || input.Bits(90, 1) ? 2 : 1);
        break;
    }
    default:
        result.valid = false;
        return result;
    }
    std::array<bool, 256> used{};
    std::array<bool, 64> groups{};
    for (const auto [bit, count] : operands) {
        const u32 reg = input.Bits(bit, 8);
        if (reg == 255)
            continue;
        if (reg + count > 255 || (staged_scratch && reg + count > 250) ||
            (reg < reserved && reg + count > reserved) ||
            (reg >= reserved && reg / 4 != (reg + count - 1) / 4)) {
            result.valid = false;
            return result;
        }
        for (u32 component = 0; component < count; ++component) {
            used[reg + component] = true;
            if (reg + component >= reserved)
                groups[(reg + component) / 4] = true;
        }
    }
    std::array<u32, 64> mapped{};
    for (u32 group = 0; group < groups.size(); ++group) {
        if (!groups[group])
            continue;
        u32 borrow = borrow_limit;
        while (borrow >= 4) {
            borrow -= 4;
            if (!used[borrow] && !used[borrow + 1] && !used[borrow + 2] && !used[borrow + 3])
                break;
        }
        if (used[borrow] || used[borrow + 1] || used[borrow + 2] || used[borrow + 3]) {
            result.valid = false;
            return result;
        }
        mapped[group] = borrow;
        for (u32 component = 0; component < 4; ++component) {
            const u32 low = borrow + component, high = group * 4 + component;
            used[low] = true;
            result.prefix.push_back(NativeRegisterCopy(2, low, 0, low));
            result.prefix.push_back(NativeRegisterCopy(0, low, 1, high));
            result.suffix.push_back(NativeRegisterCopy(1, high, 0, low));
            result.suffix.push_back(NativeRegisterCopy(0, low, 2, low));
        }
    }
    for (const auto [bit, count] : operands) {
        const u32 reg = input.Bits(bit, 8);
        if (reg == 255 || !groups[reg / 4])
            continue;
        const u32 replacement = mapped[reg / 4] + reg % 4;
        const u32 word = bit / 32, shift = bit % 32;
        result.instruction.words[word] =
            (result.instruction.words[word] & ~(255U << shift)) | (replacement << shift);
    }
    return result;
}

} // namespace Shader::Ampere
