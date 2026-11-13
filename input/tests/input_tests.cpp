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

TEST_CASE("a pad SDL does not know is sorted by its name") {
    CHECK(FamilyFromName("Nintendo Switch Pro Controller") == PadFamily::Nintendo);
    CHECK(FamilyFromName("8BitDo Pro 2 (Switch mode)") == PadFamily::Nintendo);
    CHECK(FamilyFromName("DualSense Wireless Controller") == PadFamily::PlayStation);
    CHECK(FamilyFromName("Sony Interactive Entertainment Controller") == PadFamily::PlayStation);
    CHECK(FamilyFromName("Xbox Series X Controller") == PadFamily::Xbox);
    CHECK(FamilyFromName("Generic USB Joystick") == PadFamily::Xbox);
}

TEST_CASE("the hints at start follow what exists") {
    const PadFamily sony = PadFamily::PlayStation;
    CHECK(StartHints(DeviceKind::Desktop, nullptr).family == PadFamily::Keyboard);
    CHECK(!StartHints(DeviceKind::Desktop, nullptr).touch_only);
    CHECK(StartHints(DeviceKind::Desktop, &sony).family == PadFamily::PlayStation);
    CHECK(StartHints(DeviceKind::Handheld, nullptr).family == PadFamily::Xbox);
    CHECK(StartHints(DeviceKind::Handheld, &sony).family == PadFamily::PlayStation);
    CHECK(StartHints(DeviceKind::TouchOnly, nullptr).touch_only);
    CHECK(!StartHints(DeviceKind::TouchOnly, &sony).touch_only);
}

TEST_CASE("the face buttons follow their letters on every family") {
    // Xbox and PlayStation: the bottom button confirms, the right one goes back.
    for (const PadFamily family : {PadFamily::Xbox, PadFamily::PlayStation}) {
        CHECK(NavForButton(family, PadButton::South) == Nav::Confirm);
        CHECK(NavForButton(family, PadButton::East) == Nav::Back);
        CHECK(NavForButton(family, PadButton::West) == Nav::Option);
        CHECK(NavForButton(family, PadButton::North) == Nav::Extra);
    }
    // Nintendo: A is on the right, B below, X above, Y on the left.
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::East) == Nav::Confirm);
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::South) == Nav::Back);
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::North) == Nav::Option);
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::West) == Nav::Extra);
    CHECK(NavForButton(PadFamily::Nintendo, PadButton::Guide) == Nav::Home);
}
