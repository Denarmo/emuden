// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#define DEN_CLOSE_SOCKET closesocket
#define DEN_STOP_LISTENING(s) (void)0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define DEN_CLOSE_SOCKET ::close
constexpr int INVALID_SOCKET = -1;
// close() alone does not wake a thread blocked in accept().
#define DEN_STOP_LISTENING(s) ::shutdown(s, SHUT_RDWR)
#endif

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "den/platform/pal.h"

namespace den::pal {

namespace {

class TcpLineServer final : public LineServer {
public:
    explicit TcpLineServer(SocketHandle listener) : listener_(listener) {
        thread_ = std::thread([this] { Accept(); });
    }
    ~TcpLineServer() override {
        stopping_ = true;
        DEN_STOP_LISTENING(listener_);
        DEN_CLOSE_SOCKET(listener_);
        thread_.join();
        std::lock_guard lock(mutex_);
        for (auto& [id, socket] : waiting_) {
            DEN_CLOSE_SOCKET(socket);
        }
    }

    std::optional<Request> Next() override {
        std::lock_guard lock(mutex_);
        if (pending_.empty()) {
            return std::nullopt;
        }
        Request request = std::move(pending_.front());
        pending_.pop_front();
        return request;
    }

    void Reply(std::uint64_t id, const std::string& line) override {
        SocketHandle socket;
        {
            std::lock_guard lock(mutex_);
            const auto it = waiting_.find(id);
            if (it == waiting_.end()) {
                return;
            }
            socket = it->second;
            waiting_.erase(it);
        }
        const std::string out = line + "\n";
        send(socket, out.data(), static_cast<int>(out.size()), 0);
        DEN_CLOSE_SOCKET(socket);
    }

private:
    void Accept() {
        while (!stopping_) {
            const SocketHandle client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                continue;
            }
            std::string line;
            char c = 0;
            while (line.size() < 4096 && recv(client, &c, 1, 0) == 1 && c != '\n') {
                if (c != '\r') {
                    line.push_back(c);
                }
            }
            std::lock_guard lock(mutex_);
            const std::uint64_t id = ++next_id_;
            waiting_[id] = client;
            pending_.push_back({id, std::move(line)});
        }
    }

    SocketHandle listener_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::deque<Request> pending_;
    std::map<std::uint64_t, SocketHandle> waiting_;
    std::uint64_t next_id_ = 0;
};

} // namespace

Result<std::unique_ptr<LineServer>> StartLineServer(int port) {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    const SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) {
        return Status::Fail("could not create a socket");
    }
#ifndef _WIN32
    const int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 8) != 0) {
        DEN_CLOSE_SOCKET(listener);
        return Status::Fail("could not listen on port " + std::to_string(port));
    }
    return std::unique_ptr<LineServer>(new TcpLineServer(listener));
}

} // namespace den::pal
