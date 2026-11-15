// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The platform's HTTP client against a small server on 127.0.0.1: methods, bodies from memory
// and from a slice of a file, answer headers, redirects left alone.

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/platform/pal.h"
#include "den/platform/stream.h"

using namespace den;

namespace {

struct Seen {
    std::string method, path, body;
};

// Answers `count` requests: "/redirect" with a 307, anything else with 200, the method and
// body echoed and an X-Echo-Method header.
class EchoServer {
public:
    explicit EchoServer(int count) {
        auto listener = pal::ListenLoopback();
        REQUIRE(listener);
        listener_ = std::move(listener.value());
        thread_ = std::thread([this, count] {
            for (int i = 0; i < count; ++i) {
                auto stream = listener_->Accept(10);
                if (!stream) {
                    return;
                }
                Serve(*stream.value());
            }
        });
    }
    ~EchoServer() {
        listener_->Close();
        thread_.join();
    }
    std::string Url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(listener_->Port()) + path;
    }
    Seen last;

private:
    void Serve(pal::Stream& stream) {
        std::string head;
        char c = 0;
        while (head.find("\r\n\r\n") == std::string::npos && stream.Read(&c, 1)) {
            head.push_back(c);
        }
        Seen seen;
        seen.method = head.substr(0, head.find(' '));
        const std::size_t path_at = head.find(' ') + 1;
        seen.path = head.substr(path_at, head.find(' ', path_at) - path_at);
        std::size_t length = 0;
        const std::string lower = ToLower(head);
        if (const std::size_t at = lower.find("content-length:"); at != std::string::npos) {
            length = std::stoul(head.substr(at + 15));
        }
        seen.body.resize(length);
        if (length > 0) {
            stream.Read(seen.body.data(), length);
        }
        last = seen;
        std::string reply;
        if (seen.path == "/redirect") {
            reply = "HTTP/1.1 307 Temporary Redirect\r\nLocation: /elsewhere\r\nContent-Length: 0\r\n"
                    "Connection: close\r\n\r\n";
        } else {
            const std::string body = seen.method == "HEAD" ? "" : seen.method + ":" + seen.body;
            reply = "HTTP/1.1 200 OK\r\nX-Echo-Method: " + seen.method +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body;
        }
        stream.Write(reply.data(), reply.size());
        stream.Close();
    }

    std::unique_ptr<pal::Listener> listener_;
    std::thread thread_;
};

} // namespace

TEST_CASE("http sends methods and bodies and reads answer headers", "[platform][http]") {
    EchoServer server(4);
    pal::HttpRequest request;
    request.url = server.Url("/post");
    request.method = "POST";
    request.body = "hello";
    request.headers = {{"Content-Type", "text/plain"}};
    pal::HttpResponse answer = pal::HttpGet(request);
    INFO(answer.error);
    REQUIRE(answer.ok());
    CHECK(answer.body == "POST:hello");
    CHECK(answer.Header("x-echo-method") == "POST");

    const auto file = std::filesystem::temp_directory_path() / "emuden-http-slice.bin";
    std::ofstream(file, std::ios::binary) << "0123456789";
    request = {};
    request.url = server.Url("/put");
    request.method = "PUT";
    request.body_file = file;
    request.body_offset = 3;
    request.body_length = 4;
    answer = pal::HttpGet(request);
    INFO(answer.error);
    REQUIRE(answer.ok());
    CHECK(answer.body == "PUT:3456");
    std::filesystem::remove(file);

    request = {};
    request.url = server.Url("/head");
    request.method = "HEAD";
    answer = pal::HttpGet(request);
    REQUIRE(answer.ok());
    CHECK(answer.body.empty());
    CHECK(answer.Header("x-echo-method") == "HEAD");

    request = {};
    request.url = server.Url("/redirect");
    request.follow_redirects = false;
    answer = pal::HttpGet(request);
    CHECK(answer.status == 307);
    CHECK(answer.Header("location") == "/elsewhere");
}

TEST_CASE("loopback addresses are told apart", "[platform][http]") {
    CHECK(pal::IsLoopbackUrl("https://127.0.0.1:8384/rest"));
    CHECK(pal::IsLoopbackUrl("http://LOCALHOST:8384"));
    CHECK_FALSE(pal::IsLoopbackUrl("https://127.0.0.1.example.org/"));
    CHECK_FALSE(pal::IsLoopbackUrl("https://localhost@example.org/"));
}
