// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Finding EmuDEN devices that host a game on the same network. A host broadcasts a beacon
// every second and answers the queries of devices that look for one; a browser sends those
// queries and lists what answers. Either direction alone is enough, so a network (or an
// Android device) that drops broadcasts one way still works.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "den/platform/net.h"

namespace den::net {

inline constexpr std::uint16_t DISCOVERY_PORT = 47950;
inline constexpr std::uint16_t GAME_PORT = 47951;
inline constexpr std::uint16_t PROTOCOL = 1;

/// How two devices play a game together.
enum class Mode : std::uint8_t {
    /// Each device runs its own game; the core sends link-cable or wireless packets
    /// (libretro's netpacket interface: gpSP, melonDS DS).
    Link = 1,
    /// Both devices run the same emulation frame by frame with both players' buttons
    /// (any core with save states; TGB Dual runs two linked Game Boys this way).
    Lockstep = 2,
};

/// What a host says about itself.
struct Beacon {
    std::string device;   // "Legion Go S"
    std::string title;    // the game's name
    std::string system;   // "gba"
    std::string rom_hash; // SHA-256 of the game file, hex
    std::string core;     // the core id both must play with
    Mode mode = Mode::Link;
    std::uint16_t port = GAME_PORT; // where the host listens (TCP)
    bool busy = false;              // someone plays with it already
    std::uint32_t id = 0;           // random per hosting, to tell two hosts on one device apart

    std::vector<std::uint8_t> Encode() const;
    /// False for anything that is not an EmuDEN beacon of this protocol.
    bool Decode(std::span<const std::uint8_t> data);
};

class Announcer {
public:
    /// `discovery_port` is DISCOVERY_PORT outside tests.
    static Result<std::unique_ptr<Announcer>> Start(Beacon beacon, std::uint16_t discovery_port = DISCOVERY_PORT);
    void Update(double now);
    void SetBusy(bool busy) {
        beacon_.busy = busy;
    }
    const Beacon& Info() const {
        return beacon_;
    }

private:
    Announcer(Beacon beacon, std::unique_ptr<pal::net::UdpSocket> socket, std::uint16_t port);
    void Broadcast(const std::vector<std::uint8_t>& data);

    Beacon beacon_;
    std::unique_ptr<pal::net::UdpSocket> socket_;
    std::uint16_t discovery_port_;
    double last_sent_ = -10;
    double interfaces_read_ = -100;
    std::vector<pal::net::Interface> interfaces_;
};

struct FoundHost {
    Beacon beacon;
    pal::net::Address address; // the host's game port
    double seen = 0;           // when it last answered (pal::NowSeconds)
};

class Browser {
public:
    static Result<std::unique_ptr<Browser>> Start(std::uint16_t discovery_port = DISCOVERY_PORT);
    void Update(double now);
    /// Hosts heard in the last few seconds, the most recently heard first.
    std::vector<FoundHost> Hosts(double now) const;

    static constexpr double FORGET_SECONDS = 4.0;

private:
    Browser(std::unique_ptr<pal::net::UdpSocket> asker, std::unique_ptr<pal::net::UdpSocket> listener,
            std::uint16_t port);
    void Heard(const pal::net::UdpSocket::Datagram& datagram, double now);

    std::unique_ptr<pal::net::UdpSocket> asker_;    // its own port: answers come back here
    std::unique_ptr<pal::net::UdpSocket> listener_; // the discovery port, for beacons; may be null
    std::uint16_t discovery_port_;
    double last_asked_ = -10;
    std::vector<FoundHost> hosts_;
};

} // namespace den::net
