// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// From a pad as den::input reports it (buttons by position) to a libretro RetroPad.

#pragma once

#include <cstdint>
#include <string_view>

#include "den/input/input.h"
#include "den/player/buttons.h"

namespace den::player {

struct RetroPad {
    std::uint16_t buttons = 0; // RetroButton bits
    std::int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

/// The console's own pad has A on the right and B below it (Nintendo consoles).
bool NintendoLayout(std::string_view system);

/// Buttons through `map`; triggers count as L2 and R2 when pulled; on consoles without a
/// stick the left stick is the D-pad too.
RetroPad MapPad(const input::PadState& pad, std::string_view system, const ButtonMap& map);
/// With the default map (see DefaultButtons).
RetroPad MapPad(const input::PadState& pad, std::string_view system, bool by_label);

} // namespace den::player
