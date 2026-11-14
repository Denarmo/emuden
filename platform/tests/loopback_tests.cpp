// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "den/platform/stream.h"

using namespace den;

TEST_CASE("the loopback listener takes 127.0.0.1", "[platform][loopback]") {
    auto listener = pal::ListenLoopback();
    REQUIRE(listener);
    const int port = listener.value()->Port();
    std::thread client([port] {
        auto stream = pal::ConnectLoopback(port);
        REQUIRE(stream);
        stream.value()->Write("hi", 2);
    });
    auto accepted = listener.value()->Accept(5);
    client.join();
    REQUIRE(accepted);
    char text[2] = {};
    CHECK(accepted.value()->Read(text, 2));
    CHECK(text[0] == 'h');
}

// Java's InetAddress.getLoopbackAddress() is ::1 on Android: engines built on it connect there.
TEST_CASE("the loopback listener takes ::1 too", "[platform][loopback]") {
    auto listener = pal::ListenLoopback();
    REQUIRE(listener);
    const int port = listener.value()->Port();
    std::thread client([port] {
        const auto socket = ::socket(AF_INET6, SOCK_STREAM, 0);
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(static_cast<unsigned short>(port));
        address.sin6_addr = in6addr_loopback;
        if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            send(socket, "hi", 2, 0);
        }
#ifdef _WIN32
        closesocket(socket);
#else
        ::close(socket);
#endif
    });
    auto accepted = listener.value()->Accept(5);
    client.join();
    REQUIRE(accepted);
    char text[2] = {};
    CHECK(accepted.value()->Read(text, 2));
    CHECK(text[1] == 'i');
}
