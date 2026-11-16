// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// RetroAchievements in the app: the runtime signed in with the token the plugin
// (emuden.retroachievements) kept, the running game identified and checked every frame,
// pop-ups, the unlock sound and screenshot, and the "achievements" page of the pause menu.
// Nothing goes through EmuDEN's sync: RetroAchievements keeps the progress.

#pragma once

#include <memory>

#include "den/app/app.h"
#include "den/plugins/feature_achievements.h"

namespace den::app {

/// What den.achievements of the plugin reaches.
std::shared_ptr<plugins::AchievementServices> AchievementServicesSvc();
/// Whether the game opens in hardcore (the plugin is on, signed in, hardcore chosen, and the
/// console has achievements).
bool AchievementsHardcore(const library::Entry& game);
/// The game running now has achievements loaded (the pause menu's row).
bool AchievementsForRunningGame();
/// Pop-ups, progress, challenges and trackers over the game picture.
void DrawAchievementOverlay(ui::Context& ctx, const gfx::Rect& area);

} // namespace den::app
