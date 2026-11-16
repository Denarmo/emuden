// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Play together: a game played with another EmuDEN device on the same network. Which way
// each console plays (PlanFor), and the one party of the app, shared by the game page, the
// "together" page, the player screen and its menu.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "den/library/library.h"
#include "den/net/discovery.h"
#include "den/net/party.h"
#include "den/player/session.h"
#include "den/plugins/host.h"

namespace den::app {

struct TogetherPlan {
    bool possible = false;
    std::string reason;  // why not, when not possible
    net::Mode mode = net::Mode::Lockstep;
    std::string core_id; // the core both devices play with
    std::string how;     // what it does, for the page
    unsigned subsystem = 0; // TGB Dual: both Game Boys on each device
    plugins::NetplaySpec netplay; // the core's, from its manifest
};

/// How this game is played together: a link-cable or wireless core where the console's
/// games link that way (Game Boy, GBA, DS), lockstep netplay with the console's core else.
TogetherPlan PlanFor(const library::Entry& game);
/// The plan's core on this device (installed or not).
std::optional<plugins::CoreInfo> TogetherCore(const library::Entry& game, const TogetherPlan& plan);

class Together {
public:
    static Together& Get();

    /// The party of `game_id`, or of any game when empty; null when there is none.
    std::shared_ptr<net::Party> PartyFor(const std::string& game_id = {}) const;
    const std::string& GameId() const {
        return game_id_;
    }
    /// What the party is doing, for a row ("Waiting for a player", "With Retroid · 12 ms").
    std::string StatusText() const;

    /// The game file's SHA-256, worked out on a job the first time (empty until then).
    std::string RomHash(const library::Entry& game);

    Status Host(const library::Entry& game, const TogetherPlan& plan);
    Status Join(const library::Entry& game, const TogetherPlan& plan, const net::FoundHost& host);
    void Leave();

    /// Finding hosts, while a page lists them.
    net::Browser* Browse();
    void StopBrowsing();

    /// The game is started again for the party (with its core): the player screen that
    /// leaves for it does not end the party.
    void SetRestarting(bool on) {
        restarting_ = on;
    }
    bool Restarting() const {
        return restarting_;
    }
    const TogetherPlan& Plan() const {
        return plan_;
    }

    /// What the player's session needs to play the party's game.
    player::TogetherRequest RequestFor(const library::Entry& game) const;
    /// Once a frame: reads the network, shows what happened.
    void Update();

private:
    net::GameInfo InfoFor(const library::Entry& game, const TogetherPlan& plan) const;

    std::shared_ptr<net::Party> party_;
    std::string game_id_;
    TogetherPlan plan_;
    std::unique_ptr<net::Browser> browser_;
    double browse_used_ = 0;
    bool restarting_ = false;
};

/// Opens the game for the party: from its page, or again over the player screen of
/// `game_id` when that runs (with the party's core, from the last in-game save).
void StartTogetherGame(const std::string& game_id, bool over_player);

// For the player screen.
/// The core to open `game` with: the party's when `together` and there is a party for it.
std::optional<plugins::CoreInfo> PlayerCoreFor(const library::Entry& game, bool together);
/// The session's part in the party (none when `together` is false or there is no party).
player::TogetherRequest PlayerTogetherFor(const library::Entry& game, bool together);
/// The player left `game_id`: its party ends, unless the game is only starting again.
void PlayerLeft(const std::string& game_id);
/// "Waiting for Retroid", or that it paused.
std::string TogetherWaitText();

/// This device as the others list it.
std::string DeviceLabel();

} // namespace den::app
