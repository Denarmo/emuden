// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
using SocketHandle = SOCKET;
#define DEN_CLOSE_SOCKET closesocket
#define DEN_POLL WSAPoll
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET = -1;
#define DEN_CLOSE_SOCKET ::close
#define DEN_POLL ::poll
#endif

#include <array>
#include <cstdio>
#include <mutex>

#include "den/platform/net.h"

namespace den::pal::net {

namespace {

void StartSockets() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

bool WouldBlock() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
#endif
}

void SetNonBlocking(SocketHandle socket) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(socket, FIONBIO, &on);
#else
    fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#endif
}

void SetFlag(SocketHandle socket, int level, int name) {
    const int on = 1;
    setsockopt(socket, level, name, reinterpret_cast<const char*>(&on), sizeof(on));
}

sockaddr_in ToSockaddr(const Address& address) {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(address.port);
    out.sin_addr.s_addr = htonl(address.ip);
    return out;
}

Address FromSockaddr(const sockaddr_in& address) {
    return {ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)};
}

std::uint16_t BoundPort(SocketHandle socket) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length);
    return ntohs(address.sin_port);
}

class Udp final : public UdpSocket {
public:
    Udp(SocketHandle socket, std::uint16_t port) : socket_(socket), port_(port) {}
    ~Udp() override {
        DEN_CLOSE_SOCKET(socket_);
    }
    std::uint16_t Port() const override {
        return port_;
    }
    bool SendTo(const Address& to, std::span<const std::uint8_t> data) override {
        const sockaddr_in address = ToSockaddr(to);
        return sendto(socket_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                      reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == static_cast<int>(data.size());
    }
    std::optional<Datagram> Receive() override {
        std::array<char, 2048> buffer;
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const auto got = recvfrom(socket_, buffer.data(), static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&from), &length);
        if (got <= 0) {
            return std::nullopt;
        }
        Datagram datagram;
        datagram.from = FromSockaddr(from);
        datagram.data.assign(buffer.begin(), buffer.begin() + got);
        return datagram;
    }

private:
    SocketHandle socket_;
    std::uint16_t port_;
};

class Tcp final : public TcpConnection {
public:
    Tcp(SocketHandle socket, Address peer, bool ready) : socket_(socket), peer_(peer), ready_(ready) {
        SetFlag(socket_, IPPROTO_TCP, TCP_NODELAY);
    }
    ~Tcp() override {
        DEN_CLOSE_SOCKET(socket_);
    }
    bool Ready() override {
        if (ready_ || failed_) {
            return ready_;
        }
        pollfd fd{};
        fd.fd = socket_;
        fd.events = POLLOUT;
        if (DEN_POLL(&fd, 1, 0) <= 0) {
            return false;
        }
        int error = 0;
        socklen_t length = sizeof(error);
        getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        if (error != 0 || (fd.revents & (POLLERR | POLLHUP)) != 0) {
            failed_ = true;
            return false;
        }
        ready_ = true;
        return true;
    }
    long Send(std::span<const std::uint8_t> data) override {
        if (failed_) {
            return -1;
        }
        if (!Ready()) {
            return failed_ ? -1 : 0;
        }
#ifdef MSG_NOSIGNAL
        const auto sent = send(socket_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()),
                               MSG_NOSIGNAL);
#else
        const auto sent = send(socket_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#endif
        if (sent < 0) {
            if (WouldBlock()) {
                return 0;
            }
            failed_ = true;
            return -1;
        }
        return static_cast<long>(sent);
    }
    long Receive(std::span<std::uint8_t> out) override {
        if (failed_) {
            return -1;
        }
        if (!Ready()) {
            return failed_ ? -1 : 0;
        }
        const auto got = recv(socket_, reinterpret_cast<char*>(out.data()), static_cast<int>(out.size()), 0);
        if (got == 0) {
            failed_ = true;
            return -1;
        }
        if (got < 0) {
            if (WouldBlock()) {
                return 0;
            }
            failed_ = true;
            return -1;
        }
        return static_cast<long>(got);
    }
    Address Peer() const override {
        return peer_;
    }

private:
    SocketHandle socket_;
    Address peer_;
    bool ready_ = false;
    bool failed_ = false;
};

class Listener final : public TcpListener {
public:
    Listener(SocketHandle socket, std::uint16_t port) : socket_(socket), port_(port) {}
    ~Listener() override {
        DEN_CLOSE_SOCKET(socket_);
    }
    std::uint16_t Port() const override {
        return port_;
    }
    std::unique_ptr<TcpConnection> Accept() override {
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const SocketHandle client = accept(socket_, reinterpret_cast<sockaddr*>(&from), &length);
        if (client == INVALID_SOCKET) {
            return nullptr;
        }
        SetNonBlocking(client);
        return std::make_unique<Tcp>(client, FromSockaddr(from), true);
    }

private:
    SocketHandle socket_;
    std::uint16_t port_;
};

} // namespace

std::string Address::ToString() const {
    char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return port != 0 ? std::string(text) + ":" + std::to_string(port) : std::string(text);
}

std::optional<Address> Address::Parse(const std::string& text) {
    unsigned a = 0, b = 0, c = 0, d = 0, port = 0;
    char tail = 0;
    const int read = std::sscanf(text.c_str(), "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &port, &tail);
    if ((read != 4 && read != 5) || a > 255 || b > 255 || c > 255 || d > 255 || port > 65535) {
        return std::nullopt;
    }
    return Address{(a << 24) | (b << 16) | (c << 8) | d, static_cast<std::uint16_t>(read == 5 ? port : 0)};
}

Result<std::unique_ptr<UdpSocket>> OpenUdp(std::uint16_t port, bool shared) {
    StartSockets();
    const SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) {
        return Status::Fail("could not create a socket");
    }
    SetFlag(socket, SOL_SOCKET, SO_BROADCAST);
    if (shared) {
        SetFlag(socket, SOL_SOCKET, SO_REUSEADDR);
#ifdef SO_REUSEPORT
        SetFlag(socket, SOL_SOCKET, SO_REUSEPORT);
#endif
    }
    const sockaddr_in address = ToSockaddr({0, port});
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        DEN_CLOSE_SOCKET(socket);
        return Status::Fail("UDP port " + std::to_string(port) + " is taken");
    }
    SetNonBlocking(socket);
    return std::unique_ptr<UdpSocket>(new Udp(socket, BoundPort(socket)));
}

Result<std::unique_ptr<TcpListener>> ListenTcp(std::uint16_t port) {
    StartSockets();
    const SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        return Status::Fail("could not create a socket");
    }
#ifndef _WIN32
    // A port left in TIME_WAIT by the last session can be taken again at once (on Windows
    // this flag would let two programs share the port instead).
    SetFlag(socket, SOL_SOCKET, SO_REUSEADDR);
#endif
    const sockaddr_in address = ToSockaddr({0, port});
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(socket, 4) != 0) {
        DEN_CLOSE_SOCKET(socket);
        return Status::Fail("TCP port " + std::to_string(port) + " is taken");
    }
    SetNonBlocking(socket);
    return std::unique_ptr<TcpListener>(new Listener(socket, BoundPort(socket)));
}

Result<std::unique_ptr<TcpConnection>> ConnectTcp(const Address& to) {
    StartSockets();
    const SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        return Status::Fail("could not create a socket");
    }
    SetNonBlocking(socket);
    const sockaddr_in address = ToSockaddr(to);
    const int result = connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result != 0 && !WouldBlock()) {
        DEN_CLOSE_SOCKET(socket);
        return Status::Fail("could not connect to " + to.ToString());
    }
    return std::unique_ptr<TcpConnection>(new Tcp(socket, to, result == 0));
}

std::vector<Interface> LocalInterfaces() {
    StartSockets();
    std::vector<Interface> out;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        if (GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size) != NO_ERROR) {
            return out;
        }
    }
    for (auto* adapter = list; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            if (address->sin_family != AF_INET) {
                continue;
            }
            const std::uint32_t ip = ntohl(address->sin_addr.s_addr);
            const unsigned bits = unicast->OnLinkPrefixLength;
            const std::uint32_t mask = bits == 0 ? 0 : bits >= 32 ? 0xffffffffu : ~((1u << (32 - bits)) - 1);
            out.push_back({ip, ip | ~mask});
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return out;
    }
    for (ifaddrs* item = list; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || (item->ifa_flags & IFF_UP) == 0 ||
            (item->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        const std::uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_addr)->sin_addr.s_addr);
        std::uint32_t mask = 0xffffff00u;
        if (item->ifa_netmask) {
            mask = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_netmask)->sin_addr.s_addr);
        }
        out.push_back({ip, ip | ~mask});
    }
    freeifaddrs(list);
#endif
    return out;
}

} // namespace den::pal::net
