// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Starting a game: the one place the launcher hands a library entry to den::player (ROMs) or
// den::engine (games that run outside the app), and asks for a missing core first.

#pragma once

#include <string>

namespace den::app {

/// Starts the library entry `game_id`. A ROM whose console's core is not installed asks to
/// download it first (the "install" screen) and starts afterwards. Everything that can go
/// wrong ends in a toast or a dialog; nothing is thrown.
/// `restart` starts a ROM from the beginning instead of where it was left.
void StartGame(const std::string& game_id, bool restart = false);

} // namespace den::app
