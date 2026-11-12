// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "den/input/input.h"

using namespace den::input;

TEST_CASE("confirm follows the label, not the position") {
    CHECK(NavForButton(PadFamily::Xbox, PadButton::South) == Nav::Confirm);
    CHECK(NavForButton(PadFamily::PlayStation, PadButton::South) == Nav::Confirm);
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::East) == Nav::Confirm);
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::South) == Nav::Back);
    CHECK(ButtonForNav(PadFamily::Nintendo, Nav::Confirm) == PadButton::East);
    CHECK(ButtonForNav(PadFamily::Xbox, Nav::Option) == PadButton::West);
    CHECK(ButtonForNav(PadFamily::Nintendo, Nav::Option) == PadButton::North);
}

TEST_CASE("glyphs are per family") {
    CHECK(std::string(GlyphName(PadFamily::Xbox, Nav::Confirm)) == "xbox_a");
    CHECK(std::string(GlyphName(PadFamily::PlayStation, Nav::Confirm)) == "ps_cross");
    CHECK(std::string(GlyphName(PadFamily::Nintendo, Nav::Menu)) == "switch_plus");
    CHECK(std::string(GlyphLabel(PadFamily::Keyboard, Nav::Back)) == "Esc");
    CHECK(NavFromName("confirm") == Nav::Confirm);
    CHECK(NavFromName("nope") == Nav::None);
}
