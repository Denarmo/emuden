// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/platform/webview.h"

using namespace den;

TEST_CASE("web addresses give their host") {
    CHECK(pal::WebUrlHost("https://Example.com/a?b#c") == "example.com");
    CHECK(pal::WebUrlHost("http://127.0.0.1:8080/x") == "127.0.0.1");
    CHECK(pal::WebUrlHost("https://example.com@evil.test/") == "evil.test");
    CHECK(pal::WebUrlHost("https://[::1]:99/") == "::1");
    CHECK(pal::WebUrlHost("https://example.com.") == "example.com");
    CHECK(pal::WebUrlHost("file:///C:/x").empty());
    CHECK(pal::WebUrlHost("javascript:alert(1)").empty());
    CHECK(pal::WebUrlHost("example.com").empty());
    CHECK(pal::WebUrlScheme("ABOUT:blank") == "about");
}

TEST_CASE("only allowed hosts are allowed") {
    const std::vector<std::string> allowed = {"example.com", "*.files.test", "127.0.0.1"};
    CHECK(pal::WebUrlAllowed("https://example.com/page", allowed));
    CHECK_FALSE(pal::WebUrlAllowed("https://www.example.com/", allowed));
    CHECK_FALSE(pal::WebUrlAllowed("https://notexample.com/", allowed));
    CHECK_FALSE(pal::WebUrlAllowed("https://example.com.evil.test/", allowed));
    CHECK(pal::WebUrlAllowed("https://files.test/", allowed));
    CHECK(pal::WebUrlAllowed("https://a.b.files.test/", allowed));
    CHECK_FALSE(pal::WebUrlAllowed("https://xfiles.test/", allowed));
    // http only on the machine itself.
    CHECK_FALSE(pal::WebUrlAllowed("http://example.com/", allowed));
    CHECK(pal::WebUrlAllowed("http://127.0.0.1:9000/", allowed));
    CHECK_FALSE(pal::WebUrlAllowed("about:blank", allowed));
}

TEST_CASE("a suggested file name never carries a folder") {
    CHECK(pal::WebSafeFileName("hack.bps") == "hack.bps");
    CHECK(pal::WebSafeFileName("..\\..\\Windows\\x.bps") == "x.bps");
    CHECK(pal::WebSafeFileName("C:/a/b?.zip") == "b.zip");
    CHECK(pal::WebSafeFileName("../") == "download");
    CHECK(pal::WebSafeFileName("..") == "download");
}

TEST_CASE("the fake web view filters navigation and hands over downloads") {
    pal::WebViewConfig config;
    config.allowed_hosts = {"example.com"};
    auto view = pal::CreateFakeWebView(config);
    std::vector<std::string> refused;
    const auto dir = std::filesystem::temp_directory_path() / "den-webview-test";
    std::filesystem::create_directories(dir);
    std::filesystem::path finished;
    pal::WebCallbacks callbacks;
    callbacks.on_refused = [&](const pal::WebNavigation& n) { refused.push_back(n.host); };
    callbacks.on_download = [&](const pal::WebDownload& d) {
        return d.file_name.ends_with(".bps") ? pal::WebDownloadDecision::Accept(dir / d.file_name)
                                             : pal::WebDownloadDecision::Refuse();
    };
    callbacks.on_download_finished = [&](std::uint64_t, const std::filesystem::path& file) { finished = file; };
    view->SetCallbacks(callbacks);

    view->Navigate("https://example.com/");
    CHECK(view->State().url == "https://example.com/");
    CHECK(view->SimulateLink("https://example.com/hacks"));
    CHECK(view->State().can_go_back);
    CHECK_FALSE(view->SimulateLink("https://www.iana.org/domains/example"));
    CHECK(refused == std::vector<std::string>{"www.iana.org"});
    CHECK(view->State().url == "https://example.com/hacks");
    view->Back();
    CHECK(view->State().can_go_forward);

    view->SimulateDownload("https://example.com/f/game.gba", "game.gba", "application/octet-stream", "rom");
    CHECK(finished.empty());
    view->SimulateDownload("https://example.com/f/hack.bps", "sub/hack.bps", "application/octet-stream", "BPS1");
    CHECK(finished == dir / "hack.bps");
    CHECK(ReadTextFile(finished) == std::optional<std::string>("BPS1"));

    view->MovePointer(10, 20);
    view->Click(10, 20);
    view->Key(pal::WebKey::Enter);
    CHECK(view->Input() == std::vector<std::string>{"move 10 20", "click 10 20", "key Enter"});
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
