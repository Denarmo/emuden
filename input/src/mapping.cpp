// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/input/input.h"

namespace den::input {

namespace {

struct NavInfo {
    Nav nav;
    const char* name;
    // glyph asset name and badge text per family: Xbox, PlayStation, Nintendo, Keyboard
    const char* glyph[4];
    const char* label[4];
};

constexpr NavInfo NAVS[] = {
    {Nav::Up, "up", {"dpad_up", "dpad_up", "dpad_up", "key_up"}, {"\xE2\x86\x91", "\xE2\x86\x91", "\xE2\x86\x91", "\xE2\x86\x91"}},
    {Nav::Down, "down", {"dpad_down", "dpad_down", "dpad_down", "key_down"}, {"\xE2\x86\x93", "\xE2\x86\x93", "\xE2\x86\x93", "\xE2\x86\x93"}},
    {Nav::Left, "left", {"dpad_left", "dpad_left", "dpad_left", "key_left"}, {"\xE2\x86\x90", "\xE2\x86\x90", "\xE2\x86\x90", "\xE2\x86\x90"}},
    {Nav::Right, "right", {"dpad_right", "dpad_right", "dpad_right", "key_right"}, {"\xE2\x86\x92", "\xE2\x86\x92", "\xE2\x86\x92", "\xE2\x86\x92"}},
    {Nav::Confirm, "confirm", {"xbox_a", "ps_cross", "switch_a", "key_enter"}, {"A", "\xC3\x97", "A", "Enter"}},
    {Nav::Back, "back", {"xbox_b", "ps_circle", "switch_b", "key_esc"}, {"B", "O", "B", "Esc"}},
    {Nav::Option, "option", {"xbox_x", "ps_square", "switch_x", "key_x"}, {"X", "\xE2\x96\xA1", "X", "X"}},
    {Nav::Extra, "extra", {"xbox_y", "ps_triangle", "switch_y", "key_y"}, {"Y", "\xE2\x96\xB3", "Y", "Y"}},
    {Nav::Menu, "menu", {"xbox_menu", "ps_options", "switch_plus", "key_tab"}, {"Menu", "Options", "+", "Tab"}},
    {Nav::View, "view", {"xbox_view", "ps_share", "switch_minus", "key_f1"}, {"View", "Share", "-", "F1"}},
    {Nav::ShoulderL, "l", {"xbox_lb", "ps_l1", "switch_l", "key_q"}, {"LB", "L1", "L", "Q"}},
    {Nav::ShoulderR, "r", {"xbox_rb", "ps_r1", "switch_r", "key_e"}, {"RB", "R1", "R", "E"}},
    {Nav::Home, "home", {"xbox_guide", "ps_home", "switch_home", "key_home"}, {"Guide", "PS", "Home", "Home"}},
};

const NavInfo* Info(Nav nav) {
    for (const NavInfo& info : NAVS) {
        if (info.nav == nav) {
            return &info;
        }
    }
    return nullptr;
}

} // namespace

Nav NavForButton(PadFamily family, PadButton button) {
    const bool nintendo = family == PadFamily::Nintendo;
    switch (button) {
    case PadButton::South:
        return nintendo ? Nav::Back : Nav::Confirm;
    case PadButton::East:
        return nintendo ? Nav::Confirm : Nav::Back;
    case PadButton::West:
        return nintendo ? Nav::Extra : Nav::Option;
    case PadButton::North:
        return nintendo ? Nav::Option : Nav::Extra;
    case PadButton::DpadUp:
        return Nav::Up;
    case PadButton::DpadDown:
        return Nav::Down;
    case PadButton::DpadLeft:
        return Nav::Left;
    case PadButton::DpadRight:
        return Nav::Right;
    case PadButton::L1:
        return Nav::ShoulderL;
    case PadButton::R1:
        return Nav::ShoulderR;
    case PadButton::Start:
        return Nav::Menu;
    case PadButton::Select:
        return Nav::View;
    case PadButton::Guide:
        return Nav::Home;
    default:
        return Nav::None;
    }
}

PadButton ButtonForNav(PadFamily family, Nav nav) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(PadButton::Count); ++i) {
        if (NavForButton(family, static_cast<PadButton>(i)) == nav) {
            return static_cast<PadButton>(i);
        }
    }
    return PadButton::Count;
}

const char* GlyphName(PadFamily family, Nav nav) {
    const NavInfo* info = Info(nav);
    return info ? info->glyph[static_cast<int>(family)] : "";
}

const char* GlyphLabel(PadFamily family, Nav nav) {
    const NavInfo* info = Info(nav);
    return info ? info->label[static_cast<int>(family)] : "";
}

const char* NavName(Nav nav) {
    const NavInfo* info = Info(nav);
    return info ? info->name : "none";
}

Nav NavFromName(const std::string& name) {
    for (const NavInfo& info : NAVS) {
        if (name == info.name) {
            return info.nav;
        }
    }
    return Nav::None;
}

const char* FamilyName(PadFamily family) {
    switch (family) {
    case PadFamily::Xbox:
        return "xbox";
    case PadFamily::PlayStation:
        return "playstation";
    case PadFamily::Nintendo:
        return "nintendo";
    default:
        return "keyboard";
    }
}

} // namespace den::input
