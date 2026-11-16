// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A message channel to one other device over a TCP connection: framed messages, pings that
// measure the round trip, and a time-out when the other side goes quiet. Poll it every frame
// from one thread; nothing blocks.

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "den/platform/net.h"

namespace den::net {

struct Message {
    std::uint8_t type = 0;
    std::vector<std::uint8_t> body;
};

class Channel {
public:
    enum class State { Connecting, Open, Closed };

    /// `now` is pal::NowSeconds() (tests pass their own clock to Poll).
    Channel(std::unique_ptr<pal::net::TcpConnection> connection, double now);

    /// Queues a message; types below 8 belong to the channel itself.
    void Send(std::uint8_t type, std::span<const std::uint8_t> body);
    /// Writes what is queued and reads what arrived. Returns false once the channel closed.
    bool Poll(double now);
    /// The next message that arrived, in order.
    std::optional<Message> Next();
    /// Sends what is queued and closes; the other side sees the reason.
    void Close(const std::string& reason);

    State GetState() const {
        return state_;
    }
    /// Why it closed (user-facing), once Closed.
    const std::string& CloseReason() const {
        return reason_;
    }
    /// The last measured round trip in milliseconds; -1 before the first answer.
    double PingMs() const {
        return ping_ms_;
    }
    pal::net::Address Peer() const;

    static constexpr double TIMEOUT_SECONDS = 8.0;
    static constexpr double PING_SECONDS = 0.5;
    static constexpr std::uint32_t MAX_MESSAGE = 64u << 20;

private:
    void Fail(std::string reason);
    void Handle(Message message, double now);

    std::unique_ptr<pal::net::TcpConnection> connection_;
    State state_ = State::Connecting;
    std::string reason_;
    std::vector<std::uint8_t> out_;
    std::size_t out_at_ = 0;
    std::vector<std::uint8_t> in_;
    std::deque<Message> received_;
    double last_heard_ = 0;
    double last_ping_ = 0;
    double ping_ms_ = -1;
    double connect_started_ = 0;
    bool closing_ = false;
};

} // namespace den::net
