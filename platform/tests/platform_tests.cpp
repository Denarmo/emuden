// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/platform/pal.h"

using namespace den;

TEST_CASE("secrets are kept for the session") {
    pal::Secrets& secrets = pal::GetSecrets();
    CHECK_FALSE(secrets.Get("test.token"));
    CHECK(secrets.Set("test.token", "abc"));
    CHECK(secrets.Get("test.token") == std::optional<std::string>("abc"));
    secrets.Remove("test.token");
    CHECK_FALSE(secrets.Get("test.token"));
}

// Needs the network, so it is hidden: run with  den_platform_tests "[network]"
TEST_CASE("http get follows redirects and streams to a file", "[.network]") {
    pal::HttpRequest request;
    request.url = "https://github.com/Denarmo/emuden-plugins/raw/main/index.json";
    const pal::HttpResponse body = pal::HttpGet(request);
    INFO(body.error);
    REQUIRE(body.ok());
    CHECK(body.body.find("{") != std::string::npos);

    const auto file = std::filesystem::temp_directory_path() / "emuden-http-test.json";
    request.to_file = file;
    std::uint64_t seen = 0;
    request.progress = [&](std::uint64_t received, std::uint64_t) {
        seen = received;
        return true;
    };
    const pal::HttpResponse saved = pal::HttpGet(request);
    REQUIRE(saved.ok());
    CHECK(saved.body.empty());
    CHECK(seen == body.body.size());
    CHECK(ReadTextFile(file) == std::optional<std::string>(body.body));
    std::filesystem::remove(file);

    request.to_file.clear();
    request.url = "https://github.com/Denarmo/emuden-plugins/raw/main/no-such-file";
    CHECK(pal::HttpGet(request).status == 404);
}
