// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Two peers in this process, over loopback sockets.

#include <catch2/catch_test_macros.hpp>

#include "den/net/channel.h"
#include "den/net/discovery.h"
#include "den/net/party.h"
#include "den/platform/pal.h"

using namespace den;
using namespace den::net;

namespace {

// Pumps both until `done` or the time is up (real time: the sockets are real).
template <typename Step, typename Done>
bool PumpUntil(Step step, Done done, double seconds = 5.0) {
    const double end = pal::NowSeconds() + seconds;
    while (pal::NowSeconds() < end) {
        step(pal::NowSeconds());
        if (done()) {
            return true;
        }
        pal::SleepMs(2);
    }
    return false;
}

GameInfo Game(const std::string& device, Mode mode = Mode::Lockstep) {
    GameInfo info;
    info.device = device;
    info.title = "Test Game";
    info.system = "gb";
    info.rom_hash = "abc123";
    info.core = "tgbdual";
    info.core_version = "1";
    info.mode = mode;
    return info;
}

// Ports of their own, so a running EmuDEN on this machine is not disturbed.
constexpr std::uint16_t TEST_DISCOVERY = 47990;

} // namespace

TEST_CASE("a channel carries messages both ways and measures the round trip", "[net][channel]") {
    auto listener = pal::net::ListenTcp(0);
    REQUIRE(listener);
    auto connection = pal::net::ConnectTcp(pal::net::Address::Loopback(listener.value()->Port()));
    REQUIRE(connection);
    Channel client(connection.take(), pal::NowSeconds());
    std::unique_ptr<Channel> server;
    REQUIRE(PumpUntil(
        [&](double now) {
            client.Poll(now);
            if (!server) {
                if (auto accepted = listener.value()->Accept()) {
                    server = std::make_unique<Channel>(std::move(accepted), now);
                }
            }
        },
        [&] { return server != nullptr; }));

    const std::vector<std::uint8_t> big(3u << 20, 7); // several MiB: a state
    client.Send(40, std::vector<std::uint8_t>{1, 2, 3});
    client.Send(41, big);
    std::vector<Message> got;
    REQUIRE(PumpUntil(
        [&](double now) {
            client.Poll(now);
            server->Poll(now);
            while (auto message = server->Next()) {
                got.push_back(std::move(*message));
            }
        },
        [&] { return got.size() == 2 && client.PingMs() >= 0; }));
    CHECK(got[0].type == 40);
    CHECK(got[0].body == std::vector<std::uint8_t>{1, 2, 3});
    CHECK(got[1].body.size() == big.size());

    client.Close("Bye now.");
    REQUIRE(PumpUntil([&](double now) { server->Poll(now); },
                      [&] { return server->GetState() == Channel::State::Closed; }));
    CHECK(server->CloseReason() == "Bye now.");
}

TEST_CASE("a browser finds a host on this device", "[net][discovery]") {
    Beacon beacon;
    beacon.device = "Test host";
    beacon.title = "Pokemon Test";
    beacon.system = "gba";
    beacon.rom_hash = "ff00";
    beacon.core = "gpsp";
    beacon.port = 51234;
    beacon.id = 99;
    auto announcer = Announcer::Start(beacon, TEST_DISCOVERY);
    REQUIRE(announcer);
    auto browser = Browser::Start(TEST_DISCOVERY);
    REQUIRE(browser);
    REQUIRE(PumpUntil(
        [&](double now) {
            announcer.value()->Update(now);
            browser.value()->Update(now);
        },
        [&] { return !browser.value()->Hosts(pal::NowSeconds()).empty(); }));
    const FoundHost host = browser.value()->Hosts(pal::NowSeconds()).front();
    CHECK(host.beacon.title == "Pokemon Test");
    CHECK(host.beacon.core == "gpsp");
    CHECK(host.beacon.mode == Mode::Link);
    CHECK(host.address.port == 51234);

    Beacon decoded;
    CHECK_FALSE(decoded.Decode(std::vector<std::uint8_t>{1, 2, 3}));
}

TEST_CASE("a guest joins a host with the same game and is refused with another", "[net][party]") {
    auto host = Party::Host(Game("Host"), 0, TEST_DISCOVERY + 1);
    REQUIRE(host);
    const auto address = pal::net::Address::Loopback(host.value()->Port());

    GameInfo other = Game("Stranger");
    other.rom_hash = "different";
    auto refused = Party::Join(address, other);
    REQUIRE(refused);
    REQUIRE(PumpUntil(
        [&](double now) {
            host.value()->Update(now);
            refused.value()->Update(now);
        },
        [&] { return refused.value()->GetState() == Party::State::Ended; }));
    CHECK(refused.value()->EndReason().find("another version") != std::string::npos);
    CHECK(host.value()->GetState() == Party::State::Hosting);

    auto guest = Party::Join(address, Game("Guest"));
    REQUIRE(guest);
    REQUIRE(PumpUntil(
        [&](double now) {
            host.value()->Update(now);
            guest.value()->Update(now);
        },
        [&] { return host.value()->Joined() && guest.value()->Joined(); }));
    CHECK(host.value()->Peer().device == "Guest");
    CHECK(guest.value()->Peer().device == "Host");

    // The guest goes away without a word (a device switched off): the host waits again.
    guest.value().reset();
    REQUIRE(PumpUntil([&](double now) { host.value()->Update(now); },
                      [&] { return host.value()->GetState() == Party::State::Hosting; }));
    bool left = false;
    while (auto event = host.value()->NextEvent()) {
        left = left || event->kind == PartyEvent::Kind::Left;
    }
    CHECK(left);
}

TEST_CASE("link packets pass both ways in order", "[net][party]") {
    auto host = Party::Host(Game("Host", Mode::Link), 0, TEST_DISCOVERY + 2);
    REQUIRE(host);
    auto guest = Party::Join(pal::net::Address::Loopback(host.value()->Port()), Game("Guest", Mode::Link));
    REQUIRE(guest);
    const auto both = [&](double now) {
        host.value()->Update(now);
        guest.value()->Update(now);
    };
    REQUIRE(PumpUntil(both, [&] { return host.value()->Joined() && guest.value()->Joined(); }));
    for (std::uint8_t i = 0; i < 50; ++i) {
        host.value()->SendPacket(std::vector<std::uint8_t>{i, 1});
        guest.value()->SendPacket(std::vector<std::uint8_t>{i, 2});
    }
    std::vector<std::vector<std::uint8_t>> at_host, at_guest;
    REQUIRE(PumpUntil(
        [&](double now) {
            both(now);
            for (auto& packet : host.value()->TakePackets()) {
                at_host.push_back(std::move(packet));
            }
            for (auto& packet : guest.value()->TakePackets()) {
                at_guest.push_back(std::move(packet));
            }
        },
        [&] { return at_host.size() == 50 && at_guest.size() == 50; }));
    for (std::uint8_t i = 0; i < 50; ++i) {
        CHECK(at_host[i] == std::vector<std::uint8_t>{i, 2});
        CHECK(at_guest[i] == std::vector<std::uint8_t>{i, 1});
    }
}
