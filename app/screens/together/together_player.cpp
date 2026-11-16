// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What Together adds to a game the player runs: the plan's core and the connection when the
// game is played together, the pause menu's row, and the line while the other device waits.

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/player_hooks.h"
#include "together.h"

namespace den::app {

namespace {

PlayerHooks TogetherHooks() {
    PlayerHooks hooks;
    hooks.core = [](const PlayerStart& start) -> std::optional<plugins::CoreInfo> {
        if (start.Param("together") != "1") {
            return std::nullopt;
        }
        return PlayerCoreFor(start.game, true);
    };
    hooks.open = [](const PlayerStart& start, player::OpenRequest& request) {
        request.together = PlayerTogetherFor(start.game, start.Param("together") == "1");
    };
    hooks.closed = [](const library::Entry& game) { PlayerLeft(game.id); };
    hooks.slot = PauseSlot::AfterPlayers;
    hooks.pause_rows = [](const PauseContext& pause) {
        return std::vector<ui::RowSpec>{ValueRow(
            "menu.together", "Play together",
            [id = pause.game.id] {
                return Together::Get().PartyFor(id) ? Together::Get().StatusText() : std::string("Off");
            },
            [id = pause.game.id] { Svc().stage->Push("together", {{"id", id}, {"from", "game"}}); })};
    };
    hooks.status = [](const player::Snapshot& snapshot) {
        return snapshot.waiting_for_peer ? TogetherWaitText() : std::string();
    };
    return hooks;
}

DEN_REGISTER_PLAYER_HOOKS(TogetherHooks());

} // namespace

} // namespace den::app
