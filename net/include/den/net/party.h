// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Two EmuDEN devices playing one game together: the host shows itself on the network and
// takes one guest; the guest connects, both say which game and core they have, and then
// carry the game's traffic (link packets, or lockstep buttons, states and checksums).
// Main thread only: Update once a frame, and wherever the game needs fresh data.

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/net/channel.h"
#include "den/net/discovery.h"
#include "den/net/lockstep.h"

namespace den::net {

/// What one side brings.
struct GameInfo {
    std::string device; // what the other side calls this device
    std::string title;
    std::string system;
    std::string rom_hash;
    std::string core;
    std::string core_version;
    Mode mode = Mode::Link;
    /// The battery save the other side needs too (TGB Dual runs both Game Boys on each
    /// device); empty otherwise.
    std::vector<std::uint8_t> save;
};

/// Whether a guest's game can play with a host's: an empty reason when it can.
std::string Mismatch(const GameInfo& host, const GameInfo& guest);

struct PartyEvent {
    enum class Kind { Joined, Left, Refused, Desync, Paused, Resumed };
    Kind kind = Kind::Joined;
    std::string text; // user-facing
};

/// The host's state for a lockstep start or a resync: it ran up to `frame - 1`.
struct StateTransfer {
    std::uint64_t frame = 0;
    int delay = 2;
    std::vector<std::uint8_t> state;
};

class Party {
public:
    enum class State { Hosting, Connecting, Joined, Ended };

    /// Listens on `port` (another one when it is taken) and announces `info`.
    static Result<std::unique_ptr<Party>> Host(GameInfo info, std::uint16_t port = GAME_PORT,
                                               std::uint16_t discovery_port = DISCOVERY_PORT);
    static Result<std::unique_ptr<Party>> Join(const pal::net::Address& host, GameInfo info);
    ~Party();

    void Update(double now);
    /// Ends the session; the other side is told `reason`.
    void Leave(const std::string& reason = "The other player left.");

    bool IsHost() const {
        return host_;
    }
    State GetState() const {
        return state_;
    }
    bool Joined() const {
        return state_ == State::Joined;
    }
    const GameInfo& Local() const {
        return local_;
    }
    /// The other side's game, once Joined.
    const GameInfo& Peer() const {
        return peer_;
    }
    /// The round trip in milliseconds, -1 while unknown.
    double PingMs() const;
    /// Why it ended, once Ended.
    const std::string& EndReason() const {
        return end_reason_;
    }
    std::uint16_t Port() const;
    std::optional<PartyEvent> NextEvent();

    // Link mode: the core's packets.
    void SendPacket(std::span<const std::uint8_t> packet);
    std::vector<std::vector<std::uint8_t>> TakePackets();

    // Lockstep mode.
    Lockstep& Sync() {
        return sync_;
    }
    void SendInput(std::uint64_t frame, const FrameInput& input);
    void SendState(const StateTransfer& transfer);
    std::optional<StateTransfer> TakeState();
    void SendCrc(std::uint64_t frame, std::uint32_t crc);
    void RequestResync();
    bool TakeResyncRequest();
    /// The input delay a lockstep start should use, from the measured round trip.
    int ChooseDelay() const;

    /// This device's game is paused (its menu is open); the other side is told.
    void SetPaused(bool paused);
    bool PeerPaused() const {
        return peer_paused_;
    }

private:
    Party(bool host, GameInfo info);
    void Handle(const Message& message);
    void Event(PartyEvent::Kind kind, std::string text);
    void Dropped(std::string reason);
    void Accept(double now);

    bool host_;
    State state_;
    GameInfo local_, peer_;
    std::string end_reason_;
    std::unique_ptr<pal::net::TcpListener> listener_;
    std::unique_ptr<Announcer> announcer_;
    std::unique_ptr<Channel> channel_;  // the guest, or the host
    std::unique_ptr<Channel> pending_;  // a host's guest before its hello
    std::deque<PartyEvent> events_;
    std::vector<std::vector<std::uint8_t>> packets_;
    std::optional<StateTransfer> state_in_;
    bool resync_asked_ = false;
    bool paused_ = false, peer_paused_ = false;
    Lockstep sync_;
};

} // namespace den::net
