// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Which picture preset a game is shown with: a choice per game, per console or for every
// console (the layered settings), "auto" being the console's own default.

#pragma once

#include <string>
#include <vector>

#include "den/library/library.h"
#include "den/system/settings.h"

namespace den::app {

/// "player.picture": a preset id of den::player, or "auto".
extern const system::Key<std::string> PICTURE_KEY;

enum class PictureScope { Game, Console, Everything };

/// The choice in effect for a game (may be "auto").
std::string PictureFor(const library::Entry& game);
/// What "Apply to" starts at: the game when it has a choice of its own, else its console.
PictureScope PictureScopeOf(const library::Entry& game);
/// Keeps a choice; the narrower layers are cleared so it shows.
void SavePicture(const library::Entry& game, PictureScope scope, const std::string& id);

/// Every choice in menu order: "auto", then the presets.
std::vector<std::string> PictureIds();
/// "Auto: Dot matrix" for a console (the preset's name without its variant), "Auto: by
/// console" without one, else the preset's name.
std::string PictureName(const std::string& id, const std::string& system = {});

} // namespace den::app
