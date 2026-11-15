// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/net/channel.h"

#include <array>
#include <bit>

#include "den/net/wire.h"

namespace den::net {

namespace {

constexpr std::uint8_t PING = 1;
constexpr std::uint8_t PONG = 2;
constexpr std::uint8_t CLOSE = 3;
constexpr double CONNECT_SECONDS = 5.0;

std::uint64_t Micros(double seconds) {
    return static_cast<std::uint64_t>(seconds * 1e6);
}

} // namespace

Channel::Channel(std::unique_ptr<pal::net::TcpConnection> connection, double now)
    : connection_(std::move(connection)), last_heard_(now), last_ping_(now), connect_started_(now) {
    if (!connection_) {
        state_ = State::Closed;
        reason_ = "No connection.";
    }
}

pal::net::Address Channel::Peer() const {
    return connection_ ? connection_->Peer() : pal::net::Address{};
}

void Channel::Send(std::uint8_t type, std::span<const std::uint8_t> body) {
    if (state_ == State::Closed || closing_) {
        return;
    }
    Writer header;
    header.U32(static_cast<std::uint32_t>(body.size() + 1)).U8(type);
    // Written bytes are dropped from the front now and then, not per message.
    if (out_at_ > (1u << 20) && out_at_ * 2 > out_.size()) {
        out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(out_at_));
        out_at_ = 0;
    }
    out_.insert(out_.end(), header.Data().begin(), header.Data().end());
    out_.insert(out_.end(), body.begin(), body.end());
}

void Channel::Fail(std::string reason) {
    if (state_ != State::Closed) {
        state_ = State::Closed;
        reason_ = std::move(reason);
    }
}

void Channel::Close(const std::string& reason) {
    if (state_ == State::Closed) {
        return;
    }
    Writer body;
    body.Str(reason);
    Send(CLOSE, body.Data());
    closing_ = true;
    // One last try to get it out; the connection goes with the channel.
    if (connection_ && connection_->Ready()) {
        connection_->Send({out_.data() + out_at_, out_.size() - out_at_});
    }
    Fail("You left.");
}

void Channel::Handle(Message message, double now) {
    if (message.type == PING) {
        Send(PONG, message.body);
    } else if (message.type == PONG) {
        Reader reader(message.body);
        const std::uint64_t sent = reader.U64();
        if (!reader.Failed() && Micros(now) >= sent) {
            ping_ms_ = static_cast<double>(Micros(now) - sent) / 1000.0;
        }
    } else if (message.type == CLOSE) {
        Reader reader(message.body);
        const std::string reason = reader.Str();
        Fail(reason.empty() ? "The other device left." : reason);
    } else {
        received_.push_back(std::move(message));
    }
}

bool Channel::Poll(double now) {
    if (state_ == State::Closed) {
        return false;
    }
    if (state_ == State::Connecting) {
        if (connection_->Ready()) {
            state_ = State::Open;
            last_heard_ = now;
        } else if (connection_->Send({}) < 0) {
            Fail("The other device could not be reached.");
            return false;
        } else if (now - connect_started_ > CONNECT_SECONDS) {
            Fail("The other device did not answer.");
            return false;
        } else {
            return true;
        }
    }
    if (now - last_ping_ >= PING_SECONDS) {
        last_ping_ = now;
        Writer body;
        body.U64(Micros(now));
        Send(PING, body.Data());
    }
    // Write as much as the system takes.
    while (out_at_ < out_.size()) {
        const long sent = connection_->Send({out_.data() + out_at_, out_.size() - out_at_});
        if (sent < 0) {
            Fail("The connection was lost.");
            return false;
        }
        if (sent == 0) {
            break;
        }
        out_at_ += static_cast<std::size_t>(sent);
    }
    if (out_at_ == out_.size()) {
        out_.clear();
        out_at_ = 0;
    }
    // Read what is there.
    std::array<std::uint8_t, 64 * 1024> buffer;
    bool lost = false; // after the messages that came before it: the last may say why
    for (;;) {
        const long got = connection_->Receive(buffer);
        if (got < 0) {
            lost = true;
            break;
        }
        if (got == 0) {
            break;
        }
        last_heard_ = now;
        in_.insert(in_.end(), buffer.begin(), buffer.begin() + got);
    }
    std::size_t at = 0;
    while (in_.size() - at >= 5) {
        Reader reader({in_.data() + at, 4});
        const std::uint32_t length = reader.U32();
        if (length == 0 || length > MAX_MESSAGE) {
            Fail("The other device sent something EmuDEN does not understand.");
            return false;
        }
        if (in_.size() - at - 4 < length) {
            break;
        }
        Message message;
        message.type = in_[at + 4];
        message.body.assign(in_.begin() + static_cast<std::ptrdiff_t>(at + 5),
                            in_.begin() + static_cast<std::ptrdiff_t>(at + 4 + length));
        at += 4 + length;
        Handle(std::move(message), now);
    }
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(at));
    if (lost) {
        Fail("The connection was lost.");
    }
    if (state_ != State::Closed && now - last_heard_ > TIMEOUT_SECONDS) {
        Fail("The other device stopped answering.");
    }
    return state_ != State::Closed;
}

std::optional<Message> Channel::Next() {
    if (received_.empty()) {
        return std::nullopt;
    }
    Message message = std::move(received_.front());
    received_.pop_front();
    return message;
}

} // namespace den::net
