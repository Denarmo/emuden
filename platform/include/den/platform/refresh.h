// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The refresh rate of the display the main window is on, and asking for another one while
// the window is shown (black frame insertion wants a whole multiple of the game's rate).
// Android: the window's preferred display mode, which the system may still refuse (power
// saving, a peak rate set by the user). Desktop: only a full screen window can change the
// display's mode; windowed, the rate is the desktop's.

#pragma once

#include <vector>

#include "den/platform/pal.h"

namespace den::pal {

struct RefreshRates {
    float current = 0;           // what the display runs at now; 0 when unknown
    std::vector<float> available; // what it can be asked for at its current size, current included
};

/// Main thread. Cheap enough for a few calls a second.
RefreshRates MainRefreshRates(Window& window);

/// Asks for `hz` (one of RefreshRates::available) while the window is shown; 0 gives the
/// display back its own choice. It takes effect a little later, when it does at all.
void RequestMainRefresh(Window& window, float hz);

} // namespace den::pal
