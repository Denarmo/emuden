// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/net/party.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "den/base/log.h"
#include "den/net/wire.h"
#include "den/platform/pal.h"

namespace den::net {

namespace {

enum : std::uint8_t {
    HELLO = 16,
    WELCOME = 17,
    REFUSE = 18,
    PACKET = 19,
    INPUT = 20,
    STATE = 21,
    CRC = 22,
    RESYNC = 23,
    PAUSE = 24,
};

void Put(Writer& writer, const GameInfo& info) {
    writer.U16(PROTOCOL).Str(info.device).Str(info.title).Str(info.system).Str(info.rom_hash).Str(info.core);
    writer.Str(info.core_version).U8(static_cast<std::uint8_t>(info.mode)).Bytes(info.save);
}

bool Get(Reader& reader, GameInfo& info) {
    if (reader.U16() != PROTOCOL) {
        return false;
    }
    info.device = reader.Str();
    info.title = reader.Str();
    info.system = reader.Str();
    info.rom_hash = reader.Str();
    info.core = reader.Str();
    info.core_version = reader.Str();
    info.mode = static_cast<Mode>(reader.U8());
    info.save = reader.Bytes();
    return !reader.Failed();
}

std::uint32_t RandomId() {
    std::random_device device;
    return device();
}

} // namespace

std::string Mismatch(const GameInfo& host, const GameInfo& guest) {
    if (host.system != guest.system) {
        return "The host plays a game of another console.";
    }
    if (host.mode != guest.mode || host.core != guest.core) {
        return "The host plays with " + host.core + " and this device with " + guest.core +
               ". Update EmuDEN and the console on both.";
    }
    if (host.mode == Mode::Lockstep && host.rom_hash != guest.rom_hash) {
        return "The host has another version of this game. Both need the same game file.";
    }
    return {};
}

Party::Party(bool host, GameInfo info)
    : host_(host), state_(host ? State::Hosting : State::Connecting), local_(std::move(info)) {}

Party::~Party() {
    Leave();
}

Result<std::unique_ptr<Party>> Party::Host(GameInfo info, std::uint16_t port, std::uint16_t discovery_port) {
    auto listener = pal::net::ListenTcp(port);
    if (!listener) {
        // Another EmuDEN on this device hosts already: any port will do, the beacon says it.
        listener = pal::net::ListenTcp(0);
        if (!listener) {
            return listener.status();
        }
    }
    std::unique_ptr<Party> party(new Party(true, std::move(info)));
    party->listener_ = listener.take();
    Beacon beacon;
    beacon.device = party->local_.device;
    beacon.title = party->local_.title;
    beacon.system = party->local_.system;
    beacon.rom_hash = party->local_.rom_hash;
    beacon.core = party->local_.core;
    beacon.mode = party->local_.mode;
    beacon.port = party->listener_->Port();
    beacon.id = RandomId();
    auto announcer = Announcer::Start(std::move(beacon), discovery_port);
    if (!announcer) {
        return Status::Fail("This device cannot show itself on the network: " + announcer.error());
    }
    party->announcer_ = announcer.take();
    log::Info("net", "hosting {} on port {}", party->local_.title, party->listener_->Port());
    return party;
}

Result<std::unique_ptr<Party>> Party::Join(const pal::net::Address& host, GameInfo info) {
    auto connection = pal::net::ConnectTcp(host);
    if (!connection) {
        return connection.status();
    }
    std::unique_ptr<Party> party(new Party(false, std::move(info)));
    party->channel_ = std::make_unique<Channel>(connection.take(), pal::NowSeconds());
    Writer hello;
    Put(hello, party->local_);
    party->channel_->Send(HELLO, hello.Data());
    log::Info("net", "joining {}", host.ToString());
    return party;
}

std::uint16_t Party::Port() const {
    return listener_ ? listener_->Port() : 0;
}

double Party::PingMs() const {
    return channel_ ? channel_->PingMs() : -1;
}

void Party::Event(PartyEvent::Kind kind, std::string text) {
    events_.push_back({kind, std::move(text)});
}

std::optional<PartyEvent> Party::NextEvent() {
    if (events_.empty()) {
        return std::nullopt;
    }
    PartyEvent event = std::move(events_.front());
    events_.pop_front();
    return event;
}

void Party::Dropped(std::string reason) {
    const bool was_joined = state_ == State::Joined;
    channel_.reset();
    packets_.clear();
    state_in_.reset();
    sync_ = Lockstep{};
    peer_paused_ = false;
    log::Info("net", "connection ended: {}", reason);
    if (host_) {
        // Back to waiting for someone.
        state_ = State::Hosting;
        if (announcer_) {
            announcer_->SetBusy(false);
        }
        if (was_joined) {
            Event(PartyEvent::Kind::Left, peer_.device + " left: " + reason);
        }
        return;
    }
    state_ = State::Ended;
    end_reason_ = reason;
    Event(was_joined ? PartyEvent::Kind::Left : PartyEvent::Kind::Refused, reason);
}

void Party::Accept(double now) {
    while (auto connection = listener_->Accept()) {
        auto channel = std::make_unique<Channel>(std::move(connection), now);
        if (pending_ || channel_) {
            channel->Close("Someone plays with this device already.");
            continue;
        }
        pending_ = std::move(channel);
    }
    if (!pending_) {
        return;
    }
    if (!pending_->Poll(now)) {
        pending_.reset();
        return;
    }
    auto message = pending_->Next();
    if (!message) {
        return;
    }
    Reader reader(message->body);
    GameInfo guest;
    if (message->type != HELLO || !Get(reader, guest)) {
        pending_->Close("This device runs another version of EmuDEN. Update both.");
        pending_.reset();
        return;
    }
    if (const std::string reason = Mismatch(local_, guest); !reason.empty()) {
        Writer refuse;
        refuse.Str(reason);
        pending_->Send(REFUSE, refuse.Data());
        pending_->Poll(now);
        pending_->Close(reason);
        pending_.reset();
        log::Info("net", "refused {}: {}", guest.device, reason);
        return;
    }
    peer_ = std::move(guest);
    channel_ = std::move(pending_);
    Writer welcome;
    Put(welcome, local_);
    channel_->Send(WELCOME, welcome.Data());
    state_ = State::Joined;
    announcer_->SetBusy(true);
    log::Info("net", "{} joined from {}", peer_.device, channel_->Peer().ToString());
    Event(PartyEvent::Kind::Joined, peer_.device + " joined");
}

void Party::Handle(const Message& message) {
    Reader reader(message.body);
    switch (message.type) {
    case WELCOME:
        if (!host_ && state_ == State::Connecting) {
            GameInfo host;
            if (!Get(reader, host)) {
                return Dropped("The host runs another version of EmuDEN. Update both.");
            }
            peer_ = std::move(host);
            state_ = State::Joined;
            log::Info("net", "joined {}", peer_.device);
            Event(PartyEvent::Kind::Joined, "Playing with " + peer_.device);
        }
        break;
    case REFUSE:
        return Dropped(reader.Str());
    case PACKET:
        packets_.push_back(reader.Bytes());
        break;
    case INPUT: {
        const std::uint64_t frame = reader.U64();
        FrameInput input;
        input.buttons = reader.U16();
        for (std::int16_t& axis : input.axes) {
            axis = reader.I16();
        }
        if (!reader.Failed()) {
            sync_.AddRemote(frame, input);
        }
        break;
    }
    case STATE: {
        StateTransfer transfer;
        transfer.frame = reader.U64();
        transfer.delay = reader.U8();
        transfer.state = reader.Bytes();
        if (!reader.Failed()) {
            state_in_ = std::move(transfer);
        }
        break;
    }
    case CRC: {
        const std::uint64_t frame = reader.U64();
        const std::uint32_t crc = reader.U32();
        if (!reader.Failed() && sync_.Started()) {
            sync_.RemoteCrc(frame, crc);
        }
        break;
    }
    case RESYNC:
        resync_asked_ = true;
        break;
    case PAUSE: {
        const bool paused = reader.U8() != 0;
        if (paused != peer_paused_) {
            peer_paused_ = paused;
            Event(paused ? PartyEvent::Kind::Paused : PartyEvent::Kind::Resumed,
                  peer_.device + (paused ? " paused the game" : " is back"));
        }
        break;
    }
    default:
        break;
    }
}

void Party::Update(double now) {
    if (state_ == State::Ended) {
        return;
    }
    if (host_) {
        announcer_->Update(now);
        Accept(now);
    }
    if (!channel_) {
        return;
    }
    const bool open = channel_->Poll(now);
    while (auto message = channel_->Next()) {
        Handle(*message);
        if (!channel_) {
            return;
        }
    }
    if (!open) {
        Dropped(channel_->CloseReason());
    }
}

void Party::Leave(const std::string& reason) {
    if (channel_) {
        channel_->Close(reason);
    }
    if (pending_) {
        pending_->Close(reason);
    }
    channel_.reset();
    pending_.reset();
    announcer_.reset();
    listener_.reset();
    if (state_ != State::Ended) {
        state_ = State::Ended;
        end_reason_ = "You left.";
    }
}

void Party::SendPacket(std::span<const std::uint8_t> packet) {
    if (channel_ && state_ == State::Joined) {
        Writer body;
        body.Bytes(packet);
        channel_->Send(PACKET, body.Data());
    }
}

std::vector<std::vector<std::uint8_t>> Party::TakePackets() {
    return std::exchange(packets_, {});
}

void Party::SendInput(std::uint64_t frame, const FrameInput& input) {
    if (!channel_) {
        return;
    }
    Writer body;
    body.U64(frame).U16(input.buttons);
    for (const std::int16_t axis : input.axes) {
        body.I16(axis);
    }
    channel_->Send(INPUT, body.Data());
}

void Party::SendState(const StateTransfer& transfer) {
    if (!channel_) {
        return;
    }
    Writer body;
    body.U64(transfer.frame).U8(static_cast<std::uint8_t>(transfer.delay)).Bytes(transfer.state);
    channel_->Send(STATE, body.Data());
}

std::optional<StateTransfer> Party::TakeState() {
    return std::exchange(state_in_, std::nullopt);
}

void Party::SendCrc(std::uint64_t frame, std::uint32_t crc) {
    if (channel_) {
        Writer body;
        body.U64(frame).U32(crc);
        channel_->Send(CRC, body.Data());
    }
}

void Party::RequestResync() {
    if (channel_) {
        channel_->Send(RESYNC, {});
    }
}

bool Party::TakeResyncRequest() {
    return std::exchange(resync_asked_, false);
}

int Party::ChooseDelay() const {
    const double ping = PingMs();
    if (ping < 0) {
        return 3;
    }
    // Half the round trip, in frames, and one to spare.
    return std::clamp(static_cast<int>(std::ceil(ping / 2.0 / 16.7)) + 1, 2, 8);
}

void Party::SetPaused(bool paused) {
    if (paused == paused_) {
        return;
    }
    paused_ = paused;
    if (channel_) {
        Writer body;
        body.U8(paused ? 1 : 0);
        channel_->Send(PAUSE, body.Data());
    }
}

} // namespace den::net
