// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What features add to a game the player screen runs, so they attach to the player instead
// of the player calling them: achievements set hardcore and add a pause-menu row and their
// pop-ups, Together chooses the core and the connection, sync checks the saves before the
// game starts. Registered from the feature's own files:
//
//     DEN_REGISTER_PLAYER_HOOKS([] {
//         PlayerHooks hooks;
//         hooks.open = [](const PlayerStart& start, player::OpenRequest& request) { ... };
//         return hooks;
//     }());

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/app/registry.h"
#include "den/library/library.h"
#include "den/player/session.h"
#include "den/plugins/host.h"
#include "den/ui/page.h"
#include "den/ui/widgets.h"

namespace den::app {

/// The game about to start and the player page, whose route parameters say how ("together",
/// "continue", "from").
struct PlayerStart {
    const library::Entry& game;
    const ui::Page& page;
    std::string Param(const std::string& key) const {
        return page.Param(key);
    }
};

/// Something that has to happen before the game starts (sync's check for newer saves). The
/// player shows its panel in place of the game until it is done.
class PlayerGate {
public:
    enum class Step { Waiting, Rebuild, Done };
    virtual ~PlayerGate() = default;
    /// Every frame until Done; Rebuild asks for its panel to be built again.
    virtual Step Tick() = 0;
    /// Its panel, in the page's root (centred); `focus` is the row to focus, if any.
    virtual void Build(ui::Node& root, std::string& focus) = 0;
    /// The screen goes or the user backs out: it writes nothing more.
    virtual void Abandon() = 0;
};

/// Where a feature's rows go in the pause menu's main page.
enum class PauseSlot {
    AfterStates,  // after Save state and Load state
    AfterPlayers, // after Buttons and Players; not offered while an engine app plays
};

struct PauseContext {
    const library::Entry& game;
    const player::Snapshot& snapshot;
};

struct PlayerHooks {
    /// The core the game is played with, when this feature chooses it (Together's link core);
    /// nullopt leaves it to the game's own choice.
    std::function<std::optional<plugins::CoreInfo>(const PlayerStart&)> core;
    /// The game was asked for and the boot screen begins: what can start before the player
    /// screen opens (sync's check for newer saves). `params`: the player's route parameters.
    std::function<void(const library::Entry&, const plugins::CoreInfo&, const ui::Params&)> asked;
    /// Before the game starts, once its core is known: a gate that holds it, or null.
    std::function<std::unique_ptr<PlayerGate>(const PlayerStart&, const plugins::CoreInfo&)> gate;
    /// The game is about to open: the feature sets its part of the request.
    std::function<void(const PlayerStart&, player::OpenRequest&)> open;
    /// The user left the game (it is closed, or never started).
    std::function<void(const library::Entry&)> closed;
    PauseSlot slot = PauseSlot::AfterStates;
    std::function<std::vector<ui::RowSpec>(const PauseContext&)> pause_rows;
    /// A short line over the game while it is not empty (Together waiting for the other device).
    std::function<std::string(const player::Snapshot&)> status;
    /// Drawn over the game's picture each frame (achievement pop-ups).
    std::function<void(ui::Context&, const gfx::Rect&)> overlay;
};

/// In registration order.
const std::vector<PlayerHooks>& AllPlayerHooks();
void AddPlayerHooks(PlayerHooks hooks);

struct PlayerHooksRegistrar {
    explicit PlayerHooksRegistrar(PlayerHooks hooks) {
        AddPlayerHooks(std::move(hooks));
    }
};

} // namespace den::app

#define DEN_REGISTER_PLAYER_HOOKS(...)                                                            \
    static const ::den::app::PlayerHooksRegistrar DEN_CONCAT(den_player_hooks_, __LINE__)(__VA_ARGS__)
