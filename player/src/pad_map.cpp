// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>

#include "core/core.h"
#include "pad_map.h"

namespace den::player {

namespace {

constexpr float STICK_DEADZONE = 0.12f;
constexpr float TRIGGER_PRESSED = 0.35f;

std::int16_t Axis(float value) {
    if (std::fabs(value) < STICK_DEADZONE) {
        return 0;
    }
    return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
}

} // namespace

bool NintendoLayout(std::string_view system) {
    static constexpr std::array<std::string_view, 12> SYSTEMS = {
        "gb", "gbc", "gba", "nds", "3ds", "nes", "snes", "n64", "gamecube", "wii", "virtualboy",
        "pokemini"};
    return std::find(SYSTEMS.begin(), SYSTEMS.end(), system) != SYSTEMS.end();
}

RetroPad MapPad(const input::PadState& pad, std::string_view system, bool by_label) {
    using input::PadButton;
    RetroPad out;
    const auto set = [&](PadButton from, std::uint16_t to) {
        if (pad.Down(from)) {
            out.buttons |= to;
        }
    };
    // A RetroPad is laid out like a SNES pad: B below, A right, Y left, X above.
    const bool swap =
        by_label && pad.family == input::PadFamily::Xbox && NintendoLayout(system);
    set(PadButton::South, swap ? RETRO_A : RETRO_B);
    set(PadButton::East, swap ? RETRO_B : RETRO_A);
    set(PadButton::West, swap ? RETRO_X : RETRO_Y);
    set(PadButton::North, swap ? RETRO_Y : RETRO_X);
    set(PadButton::DpadUp, RETRO_UP);
    set(PadButton::DpadDown, RETRO_DOWN);
    set(PadButton::DpadLeft, RETRO_LEFT);
    set(PadButton::DpadRight, RETRO_RIGHT);
    set(PadButton::L1, RETRO_L);
    set(PadButton::R1, RETRO_R);
    set(PadButton::L2, RETRO_L2);
    set(PadButton::R2, RETRO_R2);
    set(PadButton::L3, RETRO_L3);
    set(PadButton::R3, RETRO_R3);
    set(PadButton::Start, RETRO_START);
    set(PadButton::Select, RETRO_SELECT);
    if (pad.lt > TRIGGER_PRESSED) {
        out.buttons |= RETRO_L2;
    }
    if (pad.rt > TRIGGER_PRESSED) {
        out.buttons |= RETRO_R2;
    }
    // Consoles without a stick: the left stick is the D-pad too.
    static constexpr std::array<std::string_view, 11> DIGITAL = {
        "gb", "gbc", "gba", "nds", "nes", "snes", "genesis", "sms", "gg", "pico8", "virtualboy"};
    if (std::find(DIGITAL.begin(), DIGITAL.end(), system) != DIGITAL.end()) {
        constexpr float PUSHED = 0.5f;
        out.buttons |= pad.lx < -PUSHED ? RETRO_LEFT : 0;
        out.buttons |= pad.lx > PUSHED ? RETRO_RIGHT : 0;
        out.buttons |= pad.ly < -PUSHED ? RETRO_UP : 0;
        out.buttons |= pad.ly > PUSHED ? RETRO_DOWN : 0;
    }
    out.lx = Axis(pad.lx);
    out.ly = Axis(pad.ly);
    out.rx = Axis(pad.rx);
    out.ry = Axis(pad.ry);
    return out;
}

} // namespace den::player
