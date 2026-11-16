// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The game the player screen runs, as other features reach it: the quick settings of the
// second display, the apps beside the game, sync. What features add to the game itself is in
// den/app/player_hooks.h.

#pragma once

#include <string>

#include "den/app/black_frames.h"
#include "den/app/player_view.h"

namespace den::app {

/// What is asked before a widescreen change that starts the game again.
inline constexpr const char* WIDESCREEN_TITLE = "Start the game again?";
inline constexpr const char* WIDESCREEN_TEXT =
    "Widescreen changes when the game starts. Progress that is not saved is lost.";

class GamePlayer {
public:
    /// The frame rate overlay: 0 off, 1 the numbers, 2 the numbers and a graph of the frame times.
    static constexpr int FPS_MODES = 3;

    virtual ~GamePlayer() = default;

    /// Asking something before the game starts (which saves to use).
    virtual bool InStartCheck() const = 0;
    /// Opens one page of the pause menu by itself ("options", "buttons"); Back closes it.
    virtual void OpenPage(const std::string& page) = 0;
    /// The game goes to the other display and the quick settings come to this one. A game
    /// with two screens swaps which display has its top screen.
    virtual void SwapDisplays() = 0;
    virtual int ScreenSizeIndex() const = 0;
    virtual void SetScreenSize(int index) = 0;
    virtual int FpsMode() const = 0;
    virtual void SetFpsMode(int mode) = 0;
    /// The core can make the picture widescreen (a hack or the game's own patch).
    virtual bool CanWidescreen() const = 0;
    virtual bool Widescreen() const = 0;
    /// The core reads its widescreen options only when the game starts.
    virtual bool WidescreenRestarts() const = 0;
    /// Kept for this game. When WidescreenRestarts, the game starts again from the beginning
    /// (a resumed state is not trusted to survive a reload of every core); ask first.
    virtual void SetWidescreen(bool on) = 0;
    /// The game is on the second display and the quick settings in the main window.
    virtual bool Swapped() const = 0;
    /// Pauses with the menu open, as the menu button does; nothing when it is open already.
    virtual void Pause() = 0;
    /// Saves and leaves the game, as the pause menu's "Exit to home".
    virtual void Exit() = 0;
    /// The display the game is on is covered by another app (true): it pauses without a
    /// menu and goes on by itself when the display is back (false). A game that is paused
    /// in its menu stays so.
    virtual void Hold(bool hold) = 0;
    /// The game is set to play on the second display (whether or not that is there now).
    virtual bool WantsSecond() const = 0;
    /// An engine app plays it: its window over the main one is the game, not an app beside it.
    virtual bool External() const = 0;
    /// Black frame insertion for this game (the pause menu's and the quick settings' toggle).
    virtual const BlackFrames& Bfi() const = 0;
    /// Turns it on or off and shows the rows again with what it says now.
    virtual void SetBfi(bool on) = 0;
    /// The core the game runs with ("mgba").
    virtual const std::string& CoreId() const = 0;
};

/// The screen whose game is running; null when there is none.
GamePlayer* CurrentPlayer();

/// The library id of the core game that runs now (not an engine app's); empty when none.
std::string RunningGame();

} // namespace den::app
