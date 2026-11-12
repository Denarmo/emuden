// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "core/core.h"
#include "den/store/settings.h"
#include "pad_map.h"

namespace den::player {

namespace {

using input::PadButton;

constexpr float STICK_DEADZONE = 0.12f;
constexpr float TRIGGER_PRESSED = 0.35f;

// By RetroPad bit number.
constexpr std::array<std::string_view, GAME_BUTTONS> GAME_NAMES = {
    "b", "y", "select", "start", "up", "down", "left", "right",
    "a", "x", "l",      "r",     "l2", "r2",   "l3",   "r3"};
// A RetroPad is laid out like a SNES pad: B below, A right, Y left, X above.
constexpr ButtonMap BY_POSITION = {
    PadButton::South,  PadButton::West,     PadButton::Select,   PadButton::Start,
    PadButton::DpadUp, PadButton::DpadDown, PadButton::DpadLeft, PadButton::DpadRight,
    PadButton::East,   PadButton::North,    PadButton::L1,       PadButton::R1,
    PadButton::L2,     PadButton::R2,       PadButton::L3,       PadButton::R3};
constexpr std::array<std::string_view, static_cast<std::size_t>(PadButton::Count)> PAD_NAMES = {
    "south", "east", "west", "north", "up", "down",  "left",   "right", "l1",
    "r1",    "l2",   "r2",   "l3",    "r3", "start", "select", "guide"};

std::int16_t Axis(float value) {
    if (std::fabs(value) < STICK_DEADZONE) {
        return 0;
    }
    return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
}

std::string RemapKey(std::string_view system, int button) {
    return std::format("player.buttons.{}.{}", system, GAME_NAMES[static_cast<std::size_t>(button)]);
}

} // namespace

const char* GameButtonName(int button) {
    return button >= 0 && button < GAME_BUTTONS ? GAME_NAMES[static_cast<std::size_t>(button)].data()
                                                : "";
}

int GameButtonFromName(std::string_view name) {
    const auto it = std::find(GAME_NAMES.begin(), GAME_NAMES.end(), name);
    return it == GAME_NAMES.end() ? -1 : static_cast<int>(it - GAME_NAMES.begin());
}

const char* PadButtonName(PadButton button) {
    return button < PadButton::Count ? PAD_NAMES[static_cast<std::size_t>(button)].data() : "";
}

PadButton PadButtonFromName(std::string_view name) {
    const auto it = std::find(PAD_NAMES.begin(), PAD_NAMES.end(), name);
    return it == PAD_NAMES.end() ? PadButton::Count
                                 : static_cast<PadButton>(it - PAD_NAMES.begin());
}

bool NintendoLayout(std::string_view system) {
    static constexpr std::array<std::string_view, 12> SYSTEMS = {
        "gb", "gbc", "gba", "nds", "3ds", "nes", "snes", "n64", "gamecube", "wii", "virtualboy",
        "pokemini"};
    return std::find(SYSTEMS.begin(), SYSTEMS.end(), system) != SYSTEMS.end();
}

ButtonMap DefaultButtons(std::string_view system, input::PadFamily family, bool by_label) {
    ButtonMap map = BY_POSITION;
    if (by_label && family == input::PadFamily::Xbox && NintendoLayout(system)) {
        map[0] = PadButton::East;  // B
        map[8] = PadButton::South; // A
        map[1] = PadButton::North; // Y
        map[9] = PadButton::West;  // X
    }
    return map;
}

ButtonMap LoadButtons(const store::Settings* settings, std::string_view system,
                      input::PadFamily family, bool by_label) {
    ButtonMap map = DefaultButtons(system, family, by_label);
    if (!settings) {
        return map;
    }
    for (int button = 0; button < GAME_BUTTONS; ++button) {
        const std::string key = RemapKey(system, button);
        if (!settings->Has(key)) {
            continue;
        }
        const std::string name = settings->GetString(key);
        const PadButton source = PadButtonFromName(name);
        if (source != PadButton::Count || name == "none") {
            map[static_cast<std::size_t>(button)] = source;
        }
    }
    return map;
}

void SaveButton(store::Settings& settings, std::string_view system, int button, PadButton source) {
    if (button < 0 || button >= GAME_BUTTONS) {
        return;
    }
    settings.SetString(RemapKey(system, button),
                       source == PadButton::Count ? "none" : PadButtonName(source));
}

void ResetButtons(store::Settings& settings, std::string_view system) {
    for (int button = 0; button < GAME_BUTTONS; ++button) {
        settings.Remove(RemapKey(system, button));
    }
}

bool HasRemap(const store::Settings& settings, std::string_view system) {
    for (int button = 0; button < GAME_BUTTONS; ++button) {
        if (settings.Has(RemapKey(system, button))) {
            return true;
        }
    }
    return false;
}

RetroPad MapPad(const input::PadState& pad, std::string_view system, const ButtonMap& map) {
    RetroPad out;
    for (int button = 0; button < GAME_BUTTONS; ++button) {
        const PadButton source = map[static_cast<std::size_t>(button)];
        if (source == PadButton::Count) {
            continue;
        }
        const bool trigger = (source == PadButton::L2 && pad.lt > TRIGGER_PRESSED) ||
                             (source == PadButton::R2 && pad.rt > TRIGGER_PRESSED);
        if (pad.Down(source) || trigger) {
            out.buttons |= static_cast<std::uint16_t>(1u << button);
        }
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

RetroPad MapPad(const input::PadState& pad, std::string_view system, bool by_label) {
    return MapPad(pad, system, DefaultButtons(system, pad.family, by_label));
}

} // namespace den::player
