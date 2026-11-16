// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What a single game or console overrides in the layered settings (den/system/settings.h):
// the core that plays it, the screen size, core options, and how an engine game opens.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core_options.h"
#include "den/library/library.h"
#include "den/plugins/host.h"
#include "den/system/settings.h"

namespace den::app {

/// "player.screen_size": an index into the player's screen sizes.
extern const system::Key<int> SCREEN_SIZE_KEY;
/// "player.core": the id of the core chosen for one game; empty = the console's core.
extern const system::Key<std::string> GAME_CORE_KEY;
/// "engine.screen": "app" (as EmuDEN's own window), "fullscreen" or "window".
extern const system::Key<std::string> ENGINE_SCREEN_KEY;

system::Scope ScopeOf(const library::Entry& game);

/// The core that plays this game: the game's own choice when it is installed, else the
/// console's. Its option hints carry the game's own core options. `forced` (a test's "core="
/// route parameter) picks a core for this launch only, without touching the settings.
std::optional<plugins::CoreInfo> CoreForGame(const library::Entry& game, const std::string& forced = {});
/// The game asks for a core of its own: one is chosen for it, or an installed core lists it
/// (GBA DS lists the GBA games it has a second screen for).
bool GameHasOwnCore(const library::Entry& game);
int ScreenSizeFor(const library::Entry& game);
bool EngineFullscreenFor(const library::Entry& game, bool app_fullscreen);

/// The options noted for this core when a game last ran with it.
std::vector<KnownOption> KnownOptions(const std::string& core_id);

} // namespace den::app
