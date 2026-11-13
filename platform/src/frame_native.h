// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The system's part of a window whose title bar the app draws (src/win; elsewhere nothing).

#pragma once

#include <cstdint>

namespace den::pal {

/// Rounded corners and a thin border in this colour, drawn by the system around `sdl_window`.
void StyleCustomFrame(void* sdl_window, std::uint8_t r, std::uint8_t g, std::uint8_t b);

} // namespace den::pal
