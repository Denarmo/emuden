// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The lockstep protocol between two in-process peers on loopback, with a stand-in for a core:
// its state is a number every frame mixes both players' buttons into.

#include <map>

#include <catch2/catch_test_macros.hpp>

#include "den/net/party.h"
#include "den/net/wire.h"
#include "den/platform/pal.h"

using namespace den;
using namespace den::net;

namespace {

constexpr std::uint16_t TEST_DISCOVERY = 47995;

GameInfo Game(const std::string& device) {
    GameInfo info;
    info.device = device;
    info.title = "Two players";
    info.system = "nes";
    info.rom_hash = "1234";
    info.core = "fceumm";
    info.mode = Mode::Lockstep;
    return info;
}

struct Machine {
    std::uint32_t state = 1;
    void Run(const std::array<FrameInput, 2>& inputs) {
        for (const FrameInput& input : inputs) {
            state = state * 16777619u ^ input.buttons;
        }
    }
    std::vector<std::uint8_t> Save() const {
        Writer writer;
        writer.U32(state);
        return writer.Data();
    }
    void Load(const std::vector<std::uint8_t>& data) {
        Reader reader(data);
        state = reader.U32();
    }
};

struct Peer {
    std::unique_ptr<Party> party;
    Machine machine;
    std::map<std::uint64_t, std::uint32_t> history; // the state after each frame
    int resyncs = 0;
    int stalls = 0;

    // What the session does once per display frame.
    void Step(double now, int display_frame) {
        party->Update(now);
        Lockstep& sync = party->Sync();
        if (party->IsHost()) {
            if (party->Joined() && !sync.Started()) {
                const int delay = party->ChooseDelay();
                sync.Start(0, 0, delay);
                party->SendState({.frame = 0, .delay = delay, .state = machine.Save()});
            }
            if (party->TakeResyncRequest()) {
                party->SendState({.frame = sync.Next(), .delay = sync.Delay(), .state = machine.Save()});
            }
        } else if (auto transfer = party->TakeState()) {
            machine.Load(transfer->state);
            if (!sync.Started()) {
                sync.Start(transfer->frame, 1, transfer->delay);
            } else {
                sync.Jump(transfer->frame);
                ++resyncs;
            }
        }
        if (!sync.Started()) {
            return;
        }
        if (!party->IsHost() && sync.TakeDesync()) {
            party->RequestResync();
        }
        for (int i = 0; i < 2; ++i) {
            if (sync.NeedsLocal()) {
                FrameInput input;
                input.buttons = static_cast<std::uint16_t>((display_frame * (party->IsHost() ? 3 : 7)) & 0xfff);
                const std::uint64_t frame = sync.AddLocal(input);
                party->SendInput(frame, input);
            }
            if (!sync.Ready()) {
                ++stalls;
                return;
            }
            machine.Run(sync.Inputs());
            history[sync.Next()] = machine.state;
            sync.Advance();
            if (sync.CrcDue()) {
                sync.LocalCrc(sync.Next() - 1, machine.state);
                party->SendCrc(sync.Next() - 1, machine.state);
            }
            if (i == 0 && display_frame % 3 != 0) {
                return; // mostly one frame per display frame, sometimes two
            }
        }
    }
};

std::pair<Peer, Peer> Connect(std::uint16_t discovery) {
    auto host = Party::Host(Game("Host"), 0, discovery);
    REQUIRE(host);
    auto guest = Party::Join(pal::net::Address::Loopback(host.value()->Port()), Game("Guest"));
    REQUIRE(guest);
    return {Peer{host.take()}, Peer{guest.take()}};
}

} // namespace

TEST_CASE("lockstep buttons", "[net][lockstep]") {
    Lockstep sync;
    sync.Start(10, 1, 2);
    // Frames 10 and 11 have no buttons; the first given are for 12.
    REQUIRE(sync.NeedsLocal());
    CHECK(sync.AddLocal({0x10, {}}) == 12);
    CHECK_FALSE(sync.NeedsLocal());
    CHECK(sync.Ready());
    CHECK(sync.Inputs()[1].buttons == 0);
    sync.Advance();
    sync.AddLocal({0x20, {}});
    sync.Advance();
    sync.AddLocal({0x30, {}});
    CHECK_FALSE(sync.Ready()); // frame 12 waits for the other player
    sync.AddRemote(12, {0x01, {}});
    REQUIRE(sync.Ready());
    CHECK(sync.Inputs()[0].buttons == 0x01);
    CHECK(sync.Inputs()[1].buttons == 0x10);
}

TEST_CASE("checksums find a desync once", "[net][lockstep]") {
    Lockstep sync;
    sync.Start(0, 0, 2);
    sync.LocalCrc(60, 5);
    sync.RemoteCrc(60, 5);
    CHECK_FALSE(sync.TakeDesync());
    CHECK(sync.Agreed() == 60);
    sync.RemoteCrc(120, 6);
    sync.LocalCrc(120, 7);
    CHECK(sync.TakeDesync() == 120);
    CHECK_FALSE(sync.TakeDesync());
}

TEST_CASE("two peers run the same frames from the host's state", "[net][lockstep]") {
    auto [host, guest] = Connect(TEST_DISCOVERY);
    host.machine.state = 4242; // the host had been playing: the guest starts from its state
    int display = 0;
    const double end = pal::NowSeconds() + 20;
    while ((host.history.size() < 600 || guest.history.size() < 600) && pal::NowSeconds() < end) {
        const double now = pal::NowSeconds();
        host.Step(now, display);
        guest.Step(now, display);
        ++display;
        pal::SleepMs(1);
    }
    REQUIRE(host.history.size() >= 600);
    REQUIRE(guest.history.size() >= 600);
    for (std::uint64_t frame = 0; frame < 600; ++frame) {
        REQUIRE(host.history.at(frame) == guest.history.at(frame));
    }
    CHECK(guest.resyncs == 0);
    CHECK(host.party->Sync().Agreed() >= 540);
}

TEST_CASE("a desync is mended with the host's state", "[net][lockstep]") {
    auto [host, guest] = Connect(TEST_DISCOVERY + 1);
    bool corrupted = false;
    int display = 0;
    const double end = pal::NowSeconds() + 20;
    while ((host.history.size() < 900 || guest.history.size() < 900) && pal::NowSeconds() < end) {
        const double now = pal::NowSeconds();
        host.Step(now, display);
        guest.Step(now, display);
        if (!corrupted && guest.history.size() == 200) {
            guest.machine.state ^= 0x55; // a core that is not deterministic
            corrupted = true;
        }
        ++display;
        pal::SleepMs(1);
    }
    REQUIRE(corrupted);
    CHECK(guest.resyncs >= 1);
    REQUIRE(host.history.size() >= 900);
    REQUIRE(guest.history.size() >= 900);
    for (std::uint64_t frame = 700; frame < 900; ++frame) {
        REQUIRE(host.history.at(frame) == guest.history.at(frame));
    }
}

TEST_CASE("a guest sees the host leave", "[net][lockstep]") {
    auto [host, guest] = Connect(TEST_DISCOVERY + 2);
    const double end = pal::NowSeconds() + 5;
    while (!guest.party->Joined() && pal::NowSeconds() < end) {
        host.party->Update(pal::NowSeconds());
        guest.party->Update(pal::NowSeconds());
        pal::SleepMs(2);
    }
    REQUIRE(guest.party->Joined());
    host.party->Leave("The host ended the session.");
    while (guest.party->GetState() != Party::State::Ended && pal::NowSeconds() < end + 5) {
        guest.party->Update(pal::NowSeconds());
        pal::SleepMs(2);
    }
    CHECK(guest.party->GetState() == Party::State::Ended);
    CHECK(guest.party->EndReason() == "The host ended the session.");
}
