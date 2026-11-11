// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "hid_core/frontend/auto_buttons.h"
#include "hid_core/hid_types.h"

using namespace Core::HID::AutoInput;
using Core::HID::NpadButton;

namespace {
constexpr u64 Bit(NpadButton button) {
    return static_cast<u64>(button);
}
constexpr s32 Full = 0x7fff;
} // namespace

TEST_CASE("EDEN_AUTO_BUTTONS entries become timed button masks", "[hid][auto_buttons]") {
    const auto windows = Parse("8.5,8.7,a;11,12.25,zl+plus;20,21,up+down+left+right");
    REQUIRE(windows.size() == 3);
    REQUIRE(windows[0].start == 8.5);
    REQUIRE(windows[0].end == 8.7);
    REQUIRE(windows[0].mask == Bit(NpadButton::A));
    REQUIRE(windows[1].start == 11.0);
    REQUIRE(windows[1].end == 12.25);
    REQUIRE(windows[1].mask == (Bit(NpadButton::ZL) | Bit(NpadButton::Plus)));
    REQUIRE(windows[2].mask == (Bit(NpadButton::Up) | Bit(NpadButton::Down) | Bit(NpadButton::Left) |
                                Bit(NpadButton::Right)));
}

TEST_CASE("Every EDEN_AUTO_BUTTONS name maps to its own NpadButton bit", "[hid][auto_buttons]") {
    REQUIRE(Parse("0,1,b")[0].mask == Bit(NpadButton::B));
    REQUIRE(Parse("0,1,x")[0].mask == Bit(NpadButton::X));
    REQUIRE(Parse("0,1,y")[0].mask == Bit(NpadButton::Y));
    REQUIRE(Parse("0,1,l")[0].mask == Bit(NpadButton::L));
    REQUIRE(Parse("0,1,r")[0].mask == Bit(NpadButton::R));
    REQUIRE(Parse("0,1,zr")[0].mask == Bit(NpadButton::ZR));
    REQUIRE(Parse("0,1,minus")[0].mask == Bit(NpadButton::Minus));
    REQUIRE(Parse("0,1,lsleft")[0].mask == Bit(NpadButton::StickLLeft));
    REQUIRE(Parse("0,1,lsup")[0].mask == Bit(NpadButton::StickLUp));
    REQUIRE(Parse("0,1,lsright")[0].mask == Bit(NpadButton::StickLRight));
    REQUIRE(Parse("0,1,lsdown")[0].mask == Bit(NpadButton::StickLDown));
    REQUIRE(Parse("0,1,rsleft")[0].mask == Bit(NpadButton::StickRLeft));
    REQUIRE(Parse("0,1,rsup")[0].mask == Bit(NpadButton::StickRUp));
    REQUIRE(Parse("0,1,rsright")[0].mask == Bit(NpadButton::StickRRight));
    REQUIRE(Parse("0,1,rsdown")[0].mask == Bit(NpadButton::StickRDown));
}

TEST_CASE("EDEN_AUTO_BUTTONS ignores what it cannot read", "[hid][auto_buttons]") {
    REQUIRE(Parse(nullptr).empty());
    REQUIRE(Parse("").empty());
    // A malformed entry is skipped, the entries around it still count.
    const auto windows = Parse("1,2,a;nonsense;3,b;4,5,home+b");
    REQUIRE(windows.size() == 2);
    REQUIRE(windows[0].mask == Bit(NpadButton::A));
    // An unknown name holds nothing; the known one next to it is kept.
    REQUIRE(windows[1].start == 4.0);
    REQUIRE(windows[1].mask == Bit(NpadButton::B));
}

TEST_CASE("EDEN_AUTO_BUTTONS holds a button from start to end inclusive", "[hid][auto_buttons]") {
    const auto windows = Parse("1,2,a;1.5,3,lsup");
    REQUIRE(MaskAt(windows, 0.99) == 0);
    REQUIRE(MaskAt(windows, 1.0) == Bit(NpadButton::A));
    REQUIRE(MaskAt(windows, 1.75) == (Bit(NpadButton::A) | Bit(NpadButton::StickLUp)));
    REQUIRE(MaskAt(windows, 2.0) == (Bit(NpadButton::A) | Bit(NpadButton::StickLUp)));
    REQUIRE(MaskAt(windows, 2.01) == Bit(NpadButton::StickLUp));
    REQUIRE(MaskAt(windows, 3.0) == Bit(NpadButton::StickLUp));
    REQUIRE(MaskAt(windows, 3.01) == 0);
    REQUIRE(MaskAt({}, 1.0) == 0);
}

TEST_CASE("Scripted stick directions give full deflection with nn::hid signs",
          "[hid][auto_buttons]") {
    const u64 left = Bit(NpadButton::StickLLeft);
    const u64 right = Bit(NpadButton::StickLRight);
    REQUIRE((LeftStickBits & RightStickBits) == 0);
    REQUIRE(LeftStickBits == (Bit(NpadButton::StickLLeft) | Bit(NpadButton::StickLUp) |
                              Bit(NpadButton::StickLRight) | Bit(NpadButton::StickLDown)));
    REQUIRE(RightStickBits == (Bit(NpadButton::StickRLeft) | Bit(NpadButton::StickRUp) |
                               Bit(NpadButton::StickRRight) | Bit(NpadButton::StickRDown)));
    REQUIRE(Axis(right, left, right, Full) == Full);
    REQUIRE(Axis(left, left, right, Full) == -Full);
    REQUIRE(Axis(left | right, left, right, Full) == 0);
    REQUIRE(Axis(Bit(NpadButton::A), left, right, Full) == 0);
}
