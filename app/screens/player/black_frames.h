// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Black frame insertion for the game that runs (den/player/bfi.h): whether it is on, for
// every game, a console or one game; the display's refresh rate asked for while it is, and
// given back after; and what its rows say. It depends on this device's display, so it never
// syncs.

#pragma once

#include <string>

#include "den/library/library.h"
#include "den/platform/refresh.h"
#include "den/player/session.h"
#include "den/system/settings.h"
#include "den/ui/widgets.h"

namespace den::app {

/// "player.bfi": on or off, per console and per game.
extern const system::Key<bool> BFI_KEY;
/// "player.bfi_strength": "full" (every refresh after the game's frame is black) or "half".
extern const system::Key<std::string> BFI_STRENGTH_KEY;

inline constexpr const char* BFI_LABEL = "Black frame insertion";

/// The rows of the console page and the game settings: the console's (or the game's) value.
ui::RowSpec BfiConsoleRow(const std::string& system);

class BlackFrames {
public:
    void Start(const library::Entry& game);
    /// Each frame before Advance. `show`: nothing is over the game (a menu, the start of the
    /// game) and its display is seen; `main_display`: the game is on the main window, the
    /// one drawn in step with the display (the second display is not).
    void Update(const player::Snapshot& snapshot, bool show, bool main_display);
    /// The game ends: the display gets its own rate back.
    void Stop();

    bool On() const {
        return on_;
    }
    /// Kept for this game when it has a value of its own, else for its console. The first
    /// time it is turned on a toast warns about the flicker.
    void SetOn(bool on);
    /// Black frames cannot be shown for this game here, and the reason; empty when they can.
    std::string Refusal(const player::Snapshot& snapshot) const;
    /// What the toggle says under it: the display rate and the black frames, or why not.
    std::string Status(const player::Snapshot& snapshot) const;

private:
    void ReadRates();

    library::Entry game_;
    bool started_ = false;
    bool on_ = false;
    bool main_display_ = true;
    float requested_ = 0; // asked of the display; 0 = its own choice
    pal::RefreshRates rates_;
    double rates_at_ = -100;
};

} // namespace den::app
