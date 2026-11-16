// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The start of a game: its cartridge, card or disc drops into a slot that closes, the
// screens fade to a plain background on which EmuDEN's logo slides down with a short sound,
// and the game fades in once it shows real frames. The game is started under that cover, so
// its first black frames, a change of resolution or of display are never seen.

#pragma once

#include <functional>
#include <string>

#include "den/gfx/types.h"
#include "den/library/library.h"
#include "den/ui/node.h"

namespace den::app {

/// The settings: the drop and the logo (on unless set), and the logo's sound.
inline constexpr const char* LAUNCH_ANIMATION_KEY = "ui.home.launch_animation";
inline constexpr const char* LAUNCH_SOUND_KEY = "ui.home.launch_sound";

bool LaunchAnimationOn();

/// How the game runs once it is started.
enum class BootRunner {
    Player, // in the player screen: covered until its frames come
    Engine, // in an engine app outside: covered until the app takes the display
};

/// Plays the start over the screens and runs `start` once they are covered. A start already
/// under way keeps going and this one is dropped.
void BootGame(const library::Entry& game, BootRunner runner, std::function<void()> start);

/// Where the game's picture or object is on the main display, in canvas units, for the next
/// BootGame: its cartridge lifts off from there. Without it the focused node's rect is used.
void NoteLaunchFrom(const gfx::Rect& where);

/// True while the start of `game_id` is shown: the page it came from leaves it out.
bool BootCarries(const std::string& game_id);
/// True while the boot screen covers the game: the player gives the game no buttons.
bool BootCovers();
/// True while any of it is drawn, the fade into the game included.
bool BootShowing();

/// The second display's part (second_screen.cpp draws it over its page).
void DrawBootSecond(ui::Context& ctx);

} // namespace den::app
