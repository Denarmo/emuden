// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The buttons of the game's pad (a libretro RetroPad) and which button of the player's pad
// presses each: the default by label or position, and the user's remap kept in the settings.

#pragma once

#include <array>
#include <string>
#include <string_view>

#include "den/input/input.h"

namespace den::store {
class Settings;
}

namespace den::player {

inline constexpr int GAME_BUTTONS = 16;

/// The name of a game button by its RetroPad bit number: "b", "y", "select", "start", "up",
/// "down", "left", "right", "a", "x", "l", "r", "l2", "r2", "l3", "r3". These are the names
/// plugin manifests use.
const char* GameButtonName(int button);
/// -1 when unknown.
int GameButtonFromName(std::string_view name);
/// "south", "east", ... for settings; PadButton::Count when unknown.
const char* PadButtonName(input::PadButton button);
input::PadButton PadButtonFromName(std::string_view name);

/// For each game button, the pad button that presses it (PadButton::Count = none).
using ButtonMap = std::array<input::PadButton, GAME_BUTTONS>;

/// Without a remap. `by_label`: on a console with Nintendo's layout a pad lettered the other
/// way round (Xbox) presses the game button of the same letter.
ButtonMap DefaultButtons(std::string_view system, input::PadFamily family, bool by_label);
/// The default with the user's remap for `system` applied.
ButtonMap LoadButtons(const store::Settings* settings, std::string_view system,
                      input::PadFamily family, bool by_label);
/// Remembers that `source` presses game button `button` on `system`.
void SaveButton(store::Settings& settings, std::string_view system, int button,
                input::PadButton source);
/// Forgets every remap of `system`.
void ResetButtons(store::Settings& settings, std::string_view system);
bool HasRemap(const store::Settings& settings, std::string_view system);

} // namespace den::player
