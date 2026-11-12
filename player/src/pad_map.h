// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// From a pad as den::input reports it (buttons by position) to a libretro RetroPad.

#pragma once

#include <cstdint>
#include <string_view>

#include "den/input/input.h"

namespace den::player {

struct RetroPad {
    std::uint16_t buttons = 0; // RetroButton bits
    std::int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

/// The console's own pad has A on the right and B below it (Nintendo consoles).
bool NintendoLayout(std::string_view system);

/// `by_label`: on a console with the Nintendo layout, a pad whose buttons carry other letters
/// in those places (Xbox) presses the console button of the same letter: its A is the game's
/// A. Other pads and consoles map by position.
RetroPad MapPad(const input::PadState& pad, std::string_view system, bool by_label);

} // namespace den::player
