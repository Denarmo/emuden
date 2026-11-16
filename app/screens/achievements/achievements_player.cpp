// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What achievements add to a game the player runs: hardcore mode, the pause menu's row and
// the pop-ups over the picture.

#include "den/app/achievements_glue.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/player_hooks.h"

namespace den::app {

namespace {

PlayerHooks AchievementHooks() {
    PlayerHooks hooks;
    hooks.open = [](const PlayerStart& start, player::OpenRequest& request) {
        request.hardcore = AchievementsHardcore(start.game);
    };
    hooks.slot = PauseSlot::AfterStates;
    hooks.pause_rows = [](const PauseContext&) {
        std::vector<ui::RowSpec> rows;
        if (AchievementsForRunningGame()) {
            rows.push_back(ActionRow("menu.achievements", "Achievements", [] { Svc().stage->Push("achievements"); }));
        }
        return rows;
    };
    hooks.overlay = [](ui::Context& ctx, const gfx::Rect& area) { DrawAchievementOverlay(ctx, area); };
    return hooks;
}

DEN_REGISTER_PLAYER_HOOKS(AchievementHooks());

} // namespace

} // namespace den::app
