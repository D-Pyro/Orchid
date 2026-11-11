// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "common/common_types.h"

// Scripted controller input for unattended runs that have to get through menus into gameplay:
// EDEN_AUTO_BUTTONS=<start>,<end>,<button>[+<button>...][;...] holds the named buttons between the
// two times (seconds after launch). Kept free of emulator state so the parsing can be unit tested.
namespace Core::HID::AutoInput {

/// One scripted press. `mask` uses the nn::hid::NpadButton bit positions.
struct Window {
    double start;
    double end;
    u64 mask;
};

/// Stick direction bits (NpadButton::StickLLeft .. StickRDown).
constexpr u64 LeftStickBits = 0xFULL << 16;
constexpr u64 RightStickBits = 0xFULL << 20;

/// Buttons: a b x y l r zl zr plus minus up down left right (d-pad), and full stick deflections
/// lsleft lsup lsright lsdown rsleft rsup rsright rsdown. Unknown names are ignored and an entry
/// that is not "<number>,<number>,<names>" is skipped, so a typo cannot hold a wrong button.
inline std::vector<Window> Parse(const char* text) {
    static constexpr std::array<std::pair<std::string_view, u64>, 22> names{{
        {"a", 1ULL << 0},       {"b", 1ULL << 1},      {"x", 1ULL << 2},
        {"y", 1ULL << 3},       {"l", 1ULL << 6},      {"r", 1ULL << 7},
        {"zl", 1ULL << 8},      {"zr", 1ULL << 9},     {"plus", 1ULL << 10},
        {"minus", 1ULL << 11},  {"left", 1ULL << 12},  {"up", 1ULL << 13},
        {"right", 1ULL << 14},  {"down", 1ULL << 15},  {"lsleft", 1ULL << 16},
        {"lsup", 1ULL << 17},   {"lsright", 1ULL << 18}, {"lsdown", 1ULL << 19},
        {"rsleft", 1ULL << 20}, {"rsup", 1ULL << 21},  {"rsright", 1ULL << 22},
        {"rsdown", 1ULL << 23},
    }};
    std::vector<Window> parsed;
    for (const char* cursor = text; cursor && *cursor;) {
        Window window{};
        char buttons[64]{};
        if (std::sscanf(cursor, "%lf,%lf,%63[a-z+]", &window.start, &window.end, buttons) == 3) {
            for (std::string_view rest{buttons}; !rest.empty();) {
                const size_t plus = rest.find('+');
                const std::string_view name = rest.substr(0, plus);
                for (const auto& [button, bit] : names) {
                    if (button == name) {
                        window.mask |= bit;
                    }
                }
                rest = plus == std::string_view::npos ? std::string_view{} : rest.substr(plus + 1);
            }
            parsed.push_back(window);
        }
        cursor = std::strchr(cursor, ';');
        if (cursor) {
            ++cursor;
        }
    }
    return parsed;
}

/// Buttons held at `now` seconds after launch (both ends of a window count).
inline u64 MaskAt(const std::vector<Window>& windows, double now) {
    u64 mask = 0;
    for (const auto& window : windows) {
        if (now >= window.start && now <= window.end) {
            mask |= window.mask;
        }
    }
    return mask;
}

/// Axis value of a fully deflected stick from its two direction bits; opposite directions cancel.
/// `full` is the analog maximum (0x7fff for nn::hid::AnalogStickState).
constexpr s32 Axis(u64 mask, u64 negative_bit, u64 positive_bit, s32 full) {
    return ((mask & positive_bit) ? full : 0) - ((mask & negative_bit) ? full : 0);
}

} // namespace Core::HID::AutoInput
