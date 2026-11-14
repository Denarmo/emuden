// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr int SHUT_BOTH = SD_BOTH;
#define DEN_CLOSE_SOCKET closesocket
#define DEN_POLL WSAPoll
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET = -1;
constexpr int SHUT_BOTH = SHUT_RDWR;
#define DEN_CLOSE_SOCKET ::close
#define DEN_POLL ::poll
#endif

#include <atomic>
#include <mutex>
#include <vector>

#include "den/platform/stream.h"

namespace den::pal {

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

class SocketStream final : public Stream {
public:
    explicit SocketStream(SocketHandle socket) : socket_(socket) {
        const int on = 1;
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    }
    ~SocketStream() override {
        Close();
        DEN_CLOSE_SOCKET(socket_);
    }

    bool Read(void* data, std::size_t size) override {
        auto* out = static_cast<char*>(data);
        while (size > 0) {
            const int chunk = static_cast<int>(size > (1u << 20) ? (1u << 20) : size);
            const int got = static_cast<int>(recv(socket_, out, chunk, 0));
            if (got <= 0 || closed_) {
                return false;
            }
            out += got;
            size -= static_cast<std::size_t>(got);
        }
        return true;
    }

    bool Write(const void* data, std::size_t size) override {
        std::lock_guard lock(write_);
        const auto* in = static_cast<const char*>(data);
        while (size > 0) {
            const int chunk = static_cast<int>(size > (1u << 20) ? (1u << 20) : size);
#ifdef MSG_NOSIGNAL
            const int sent = static_cast<int>(send(socket_, in, chunk, MSG_NOSIGNAL));
#else
            const int sent = static_cast<int>(send(socket_, in, chunk, 0));
#endif
            if (sent <= 0 || closed_) {
                return false;
            }
            in += sent;
            size -= static_cast<std::size_t>(sent);
        }
        return true;
    }

    void Close() override {
        if (!closed_.exchange(true)) {
            shutdown(socket_, SHUT_BOTH);
#ifdef _WIN32
            // A shutdown alone does not wake a recv blocked on another thread there.
            CancelIoEx(reinterpret_cast<HANDLE>(socket_), nullptr);
#endif
        }
    }

private:
    SocketHandle socket_;
    std::atomic<bool> closed_{false};
    std::mutex write_;
};

// Listens on 127.0.0.1 and, where the system has it, on ::1 at the same port: Java's
// InetAddress.getLoopbackAddress() is ::1 on Android, so an engine may connect there.
class SocketListener final : public Listener {
public:
    SocketListener(std::vector<SocketHandle> sockets, int port) : sockets_(std::move(sockets)), port_(port) {}
    ~SocketListener() override {
        Close();
        for (const SocketHandle socket : sockets_) {
            DEN_CLOSE_SOCKET(socket);
        }
    }

    int Port() const override {
        return port_;
    }

    Result<std::unique_ptr<Stream>> Accept(double seconds) override {
        // In slices, so a Close from another thread is seen soon. poll, not select: the
        // process may hold more descriptors than an fd_set does.
        double left = seconds;
        std::vector<pollfd> fds;
        for (const SocketHandle socket : sockets_) {
            pollfd fd{};
            fd.fd = socket;
            fd.events = POLLIN;
            fds.push_back(fd);
        }
        while (!closed_) {
            const double slice = left < 0.1 ? left : 0.1;
            for (pollfd& fd : fds) {
                fd.revents = 0;
            }
            const int ready = DEN_POLL(fds.data(), static_cast<unsigned>(fds.size()), static_cast<int>(slice * 1000));
            if (ready < 0) {
                return Status::Fail("the listener failed");
            }
            for (const pollfd& fd : fds) {
                if (ready > 0 && !closed_ && (fd.revents & POLLIN) != 0) {
                    const SocketHandle client = accept(fd.fd, nullptr, nullptr);
                    if (client == INVALID_SOCKET) {
                        return Status::Fail("the connection could not be accepted");
                    }
                    return std::unique_ptr<Stream>(new SocketStream(client));
                }
            }
            left -= slice;
            if (left <= 0) {
                return Status::Fail("nothing connected in time");
            }
        }
        return Status::Fail("the listener was closed");
    }

    void Close() override {
        closed_ = true;
    }

private:
    std::vector<SocketHandle> sockets_;
    int port_;
    std::atomic<bool> closed_{false};
};

sockaddr_in Loopback(int port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

// ::1 at the port, or nothing when the system has no IPv6 loopback or the port is taken there.
SocketHandle ListenIpv6(int port) {
    const SocketHandle listener = socket(AF_INET6, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    const int on = 1;
    setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&on), sizeof(on));
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(static_cast<unsigned short>(port));
    address.sin6_addr = in6addr_loopback;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 2) != 0) {
        DEN_CLOSE_SOCKET(listener);
        return INVALID_SOCKET;
    }
    return listener;
}

} // namespace

Result<std::unique_ptr<Listener>> ListenLoopback() {
    StartSockets();
    // A port free on both addresses; a few tries, then IPv4 alone.
    for (int attempt = 0; attempt < 8; ++attempt) {
        const SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener == INVALID_SOCKET) {
            return Status::Fail("could not create a socket");
        }
        sockaddr_in address = Loopback(0);
        socklen_t length = sizeof(address);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 2) != 0 || getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            DEN_CLOSE_SOCKET(listener);
            return Status::Fail("could not listen on the loopback address");
        }
        const int port = ntohs(address.sin_port);
        std::vector<SocketHandle> sockets = {listener};
        if (const SocketHandle v6 = ListenIpv6(port); v6 != INVALID_SOCKET) {
            sockets.push_back(v6);
        } else if (attempt < 7) {
            DEN_CLOSE_SOCKET(listener);
            continue;
        }
        return std::unique_ptr<Listener>(new SocketListener(std::move(sockets), port));
    }
    return Status::Fail("could not listen on the loopback address");
}

Result<std::unique_ptr<Stream>> ConnectLoopback(int port) {
    StartSockets();
    const SocketHandle client = socket(AF_INET, SOCK_STREAM, 0);
    if (client == INVALID_SOCKET) {
        return Status::Fail("could not create a socket");
    }
    const sockaddr_in address = Loopback(port);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        DEN_CLOSE_SOCKET(client);
        return Status::Fail("could not connect to port " + std::to_string(port));
    }
    return std::unique_ptr<Stream>(new SocketStream(client));
}

} // namespace den::pal
