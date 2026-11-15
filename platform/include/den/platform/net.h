// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Sockets on the local network, for playing together (den::net): UDP datagrams with
// broadcast, and TCP connections. Everything here is non-blocking; callers poll once a frame.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::pal::net {

/// An IPv4 address and port, both in host order.
struct Address {
    std::uint32_t ip = 0;
    std::uint16_t port = 0;

    /// "192.168.1.20:47951" (no port when it is 0).
    std::string ToString() const;
    /// "192.168.1.20" or "192.168.1.20:47951"; nullopt when it is not one.
    static std::optional<Address> Parse(const std::string& text);
    static Address Loopback(std::uint16_t port) {
        return {0x7f000001u, port};
    }
    bool operator==(const Address&) const = default;
};

class UdpSocket {
public:
    virtual ~UdpSocket() = default;
    virtual std::uint16_t Port() const = 0;
    virtual bool SendTo(const Address& to, std::span<const std::uint8_t> data) = 0;
    struct Datagram {
        Address from;
        std::vector<std::uint8_t> data;
    };
    /// The next datagram that arrived, if any.
    virtual std::optional<Datagram> Receive() = 0;
};

/// Bound to `port` on every interface (0: one the system picks), broadcast allowed. `shared`
/// lets other sockets of this and other programs bind the same port (discovery).
Result<std::unique_ptr<UdpSocket>> OpenUdp(std::uint16_t port, bool shared);

class TcpConnection {
public:
    virtual ~TcpConnection() = default;
    /// False while a connection made by Connect is still being set up.
    virtual bool Ready() = 0;
    /// Bytes written (maybe fewer than given; 0 when the system's buffer is full), -1 once the
    /// connection is gone.
    virtual long Send(std::span<const std::uint8_t> data) = 0;
    /// Bytes read into `out`, 0 when nothing is waiting, -1 once the connection is closed.
    virtual long Receive(std::span<std::uint8_t> out) = 0;
    virtual Address Peer() const = 0;
};

class TcpListener {
public:
    virtual ~TcpListener() = default;
    virtual std::uint16_t Port() const = 0;
    /// A connection that is waiting, if any.
    virtual std::unique_ptr<TcpConnection> Accept() = 0;
};

/// Listens on every interface; port 0 lets the system pick one.
Result<std::unique_ptr<TcpListener>> ListenTcp(std::uint16_t port);
/// Starts connecting; Ready() says when it is done, Send and Receive fail when it failed.
Result<std::unique_ptr<TcpConnection>> ConnectTcp(const Address& to);

struct Interface {
    std::uint32_t ip = 0;        // host order
    std::uint32_t broadcast = 0; // the subnet's broadcast address
};
/// The device's IPv4 interfaces that are up, loopback left out.
std::vector<Interface> LocalInterfaces();

} // namespace den::pal::net
