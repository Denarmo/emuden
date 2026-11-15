// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A game played with another device (den::net::Party). Two ways:
// - link: each device runs its own game and the core sends its link-cable or wireless
//   packets through the party (netpacket cores: gpSP, melonDS DS);
// - lockstep: both run the same emulation, a frame only once both players' buttons for it
//   are known; the host's state starts the guest, checksums catch a desync, and the host's
//   state mends it.

#include <cstring>
#include <format>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"
#include "den/net/party.h"
#include "den/platform/pal.h"
#include "session_impl.h"

namespace den::player {

namespace {

constexpr double WAIT_SHOWN_SECONDS = 0.25;
constexpr int MAX_OWED_FRAMES = 3;
constexpr double LOCK_TOLERANCE = 0.012;
constexpr std::uint64_t MAX_ALIGN_FRAMES = 1200;
constexpr int QUICK_RESYNCS = 3;
constexpr double RESYNC_PAUSE = 10.0;

// EMUDEN_NET_DUMP=<folder>: every checked state is written there, to find what differs
// after a desync.
void DumpCheckedState(std::uint64_t frame, bool host, std::span<const std::uint8_t> state) {
    static const std::string folder = pal::GetEnv("EMUDEN_NET_DUMP");
    if (!folder.empty()) {
        WriteFileAtomic(PathFromUtf8(folder) / std::format("{}-{}.state", host ? "host" : "guest", frame),
                        std::string_view(reinterpret_cast<const char*>(state.data()), state.size()));
    }
}

} // namespace

bool PlayerSession::LoadContent(const std::filesystem::path& content, std::string* error) {
    if (together_.party && together_.subsystem != 0) {
        // Both devices run both machines, each with the same game.
        return core_->LoadGameSpecial(together_.subsystem, {content, content}, error);
    }
    return core_->LoadGame(content, error);
}

void PlayerSession::StartTogether() {
    if (!together_.party) {
        return;
    }
    snapshot_.together = true;
    snapshot_.can_rewind = false;
    rewind_.reset();
    for (const auto& [key, value] : together_.options) {
        core_->SetOption(key, value);
    }
    if (together_.peer_save_memory != 0) {
        const std::vector<std::uint8_t>& save = together_.party->Peer().save;
        const std::span<std::uint8_t> memory = core_->Memory(together_.peer_save_memory);
        if (!memory.empty() && !save.empty()) {
            std::memcpy(memory.data(), save.data(), std::min(memory.size(), save.size()));
        }
    }
    lockstep_owed_ = 0;
    waiting_since_ = -1;
    if (together_.party->Local().mode == net::Mode::Link) {
        StartLink();
    }
    log::Info("player", "playing together ({}, {})",
              together_.party->Local().mode == net::Mode::Link ? "link" : "lockstep",
              together_.party->IsHost() ? "host" : "guest");
}

void PlayerSession::SetTogether(TogetherRequest request) {
    if (!core_ || request.subsystem != 0) {
        return;
    }
    StopTogether();
    together_ = std::move(request);
    resume_enabled_ = resume_enabled_ && !together_.party;
    StartTogether();
}

void PlayerSession::StopTogether() {
    if (core_) {
        core_->NetpacketStop();
    }
    link_started_ = false;
    peer_connected_ = false;
    together_.party.reset();
    snapshot_.together = false;
    snapshot_.waiting_for_peer = false;
}

void PlayerSession::StartLink() {
    net::Party& party = *together_.party;
    if (!core_->HasNetpacket()) {
        log::Warn("player", "{} has no link interface: the game is played alone", snapshot_.core_name);
        return;
    }
    if (!party.IsHost() && !party.Joined()) {
        return; // a guest starts once it is in
    }
    std::weak_ptr<net::Party> weak = together_.party;
    core_->NetpacketStart(
        party.IsHost() ? 0 : 1,
        [weak](std::span<const std::uint8_t> packet, std::uint16_t) {
            if (auto shared = weak.lock()) {
                shared->SendPacket(packet);
            }
        },
        [this] {
            // The core waits for an answer in the middle of a frame.
            together_.party->Update(pal::NowSeconds());
            DeliverPackets();
        });
    link_started_ = true;
    peer_connected_ = false;
}

void PlayerSession::DeliverPackets() {
    const std::uint16_t from = together_.party->IsHost() ? 1 : 0;
    for (const std::vector<std::uint8_t>& packet : together_.party->TakePackets()) {
        core_->NetpacketReceive(packet, from);
    }
}

void PlayerSession::PumpTogether() {
    net::Party& party = *together_.party;
    party.Update(pal::NowSeconds());
    if (party.Local().mode != net::Mode::Link) {
        return;
    }
    if (!link_started_) {
        StartLink();
    }
    if (!link_started_) {
        return;
    }
    if (party.IsHost()) {
        if (party.Joined() != peer_connected_) {
            peer_connected_ = party.Joined();
            if (peer_connected_) {
                core_->NetpacketConnected(1);
            } else {
                core_->NetpacketDisconnected(1);
            }
        }
    } else if (party.GetState() == net::Party::State::Ended) {
        core_->NetpacketStop();
        link_started_ = false;
        return;
    }
    DeliverPackets();
    core_->NetpacketPoll();
}

void PlayerSession::LeftLockstep() {
    // The other player's buttons stay up once they are gone.
    if (lockstep_ran_ && !InLockstep()) {
        lockstep_ran_ = false;
        snapshot_.waiting_for_peer = false;
        for (unsigned port = 1; port < MAX_PORTS; ++port) {
            core_->SetButtons(port, 0);
            core_->SetAnalog(port, 0, 0, 0);
            core_->SetAnalog(port, 1, 0, 0);
        }
    }
}

bool PlayerSession::InLockstep() const {
    return together_.party && together_.party->Local().mode == net::Mode::Lockstep &&
           together_.party->Joined();
}

bool PlayerSession::StoreLocalInput(int port, const RetroPad& mapped) {
    if (!together_.party || !together_.party->Sync().Started()) {
        return false;
    }
    // The lockstep gives each port its buttons; this device's pad is its player's.
    if (port == 0) {
        local_buttons_ = mapped.buttons;
        local_axes_ = {mapped.lx, mapped.ly, mapped.rx, mapped.ry};
    }
    return true;
}

void PlayerSession::SendStateToPeer(std::uint64_t frame) {
    net::Party& party = *together_.party;
    std::vector<std::uint8_t> state(core_->StateSize());
    if (state.empty() || !core_->SaveState(state)) {
        log::Error("player", "no state to send the other device");
        return;
    }
    // This device goes on from exactly what it sends.
    core_->LoadState(state);
    net::StateTransfer transfer;
    transfer.frame = frame;
    transfer.delay = party.Sync().Started() ? party.Sync().Delay() : party.ChooseDelay();
    transfer.core_frames = core_->FrameCount();
    transfer.state = std::move(state);
    if (!party.Sync().Started()) {
        party.Sync().Start(frame, 0, transfer.delay);
    }
    log::Info("player", "state sent for frame {} ({} bytes, {} frames delay)", frame, transfer.state.size(),
              transfer.delay);
    party.SendState(transfer);
}

void PlayerSession::TakeStateFromPeer() {
    net::Party& party = *together_.party;
    auto transfer = party.TakeState();
    if (!transfer) {
        return;
    }
    // As many frames run as the host's core has (they are undone by the state).
    if (transfer->core_frames > core_->FrameCount() &&
        transfer->core_frames - core_->FrameCount() <= MAX_ALIGN_FRAMES) {
        core_->SetAudioMuted(true);
        while (core_->FrameCount() < transfer->core_frames) {
            core_->RunFrame();
        }
        core_->SetAudioMuted(false);
    }
    std::span<const std::uint8_t> payload = transfer->state;
    const std::size_t size = core_->StateSize();
    if (payload.size() < size) {
        state_scratch_.assign(size, 0);
        std::memcpy(state_scratch_.data(), payload.data(), payload.size());
        payload = state_scratch_;
    }
    if (!core_->LoadState(payload)) {
        log::Error("player", "the host's state does not load");
        party.Leave("The host's game does not load here. Both need the same core.");
        return;
    }
    audio_.Clear();
    if (!party.Sync().Started()) {
        party.Sync().Start(transfer->frame, 1, transfer->delay);
        log::Info("player", "started from the host's state at frame {}", transfer->frame);
    } else {
        party.Sync().Jump(transfer->frame);
        log::Info("player", "resynced to the host's state at frame {}", transfer->frame);
    }
}

void PlayerSession::TickLockstep() {
    net::Party& party = *together_.party;
    net::Lockstep& sync = party.Sync();
    if (party.IsHost()) {
        // The guest starts from the host's state, after the host's first frame (a resumed
        // state is loaded then).
        if (!sync.Started()) {
            if (snapshot_.frames == 0) {
                RunForward(1, true);
            }
            SendStateToPeer(0);
        } else if (sync.Started() && party.TakeResyncRequest()) {
            SendStateToPeer(sync.Next());
        }
    } else {
        TakeStateFromPeer();
        if (sync.Started() && sync.TakeDesync()) {
            // A core whose states never compare alike would ask every second: after a few
            // quick ones, at most every RESYNC_PAUSE seconds.
            const double now = pal::NowSeconds();
            quick_resyncs_ = now - last_resync_ < RESYNC_PAUSE ? quick_resyncs_ + 1 : 0;
            if (quick_resyncs_ < QUICK_RESYNCS || now - last_resync_ >= RESYNC_PAUSE) {
                last_resync_ = now;
                log::Warn("player", "the games differ (desync): asking the host for its state");
                party.RequestResync();
            }
        }
    }
}

void PlayerSession::AdvanceLockstep(double frames) {
    net::Party& party = *together_.party;
    net::Lockstep& sync = party.Sync();
    if (std::fabs(step_ - 1.0) < LOCK_TOLERANCE) {
        due_ = 0.0;
        ++lockstep_owed_;
    } else {
        due_ += frames;
        const int count = static_cast<int>(due_);
        due_ -= count;
        lockstep_owed_ += count;
    }
    lockstep_owed_ = std::min(lockstep_owed_, MAX_OWED_FRAMES);
    bool stalled = !sync.Started();
    while (lockstep_owed_ > 0 && sync.Started()) {
        if (sync.NeedsLocal()) {
            net::FrameInput input;
            input.buttons = static_cast<std::uint16_t>(local_buttons_ | held_buttons_);
            input.axes = local_axes_;
            party.SendInput(sync.AddLocal(input), input);
        }
        if (!sync.Ready()) {
            // Read once more: the buttons may be on their way.
            party.Update(pal::NowSeconds());
            if (!sync.Ready()) {
                stalled = true;
                break;
            }
        }
        const auto inputs = sync.Inputs();
        for (unsigned port = 0; port < net::Lockstep::PLAYERS; ++port) {
            const net::FrameInput& input = inputs[port];
            core_->SetButtons(port, input.buttons);
            core_->SetAnalog(port, 0, input.axes[0], input.axes[1]);
            core_->SetAnalog(port, 1, input.axes[2], input.axes[3]);
            snapshot_.game_buttons[port] = input.buttons;
        }
        core_->RunFrame();
        lockstep_ran_ = true;
        ++snapshot_.frames;
        ++fps_frames_;
        --lockstep_owed_;
        sync.Advance();
        if (sync.CrcDue()) {
            state_scratch_.resize(core_->StateSize());
            if (!state_scratch_.empty() && core_->SaveState(state_scratch_)) {
                const std::uint32_t crc = Crc32Update(0, state_scratch_.data(), state_scratch_.size());
                DumpCheckedState(sync.Next() - 1, party.IsHost(), state_scratch_);
                sync.LocalCrc(sync.Next() - 1, crc);
                party.SendCrc(sync.Next() - 1, crc);
            }
        }
    }
    const double now = pal::NowSeconds();
    if (!stalled) {
        waiting_since_ = -1;
    } else if (waiting_since_ < 0) {
        waiting_since_ = now;
    }
    snapshot_.waiting_for_peer = waiting_since_ >= 0 && now - waiting_since_ >= WAIT_SHOWN_SECONDS;
}

} // namespace den::player
