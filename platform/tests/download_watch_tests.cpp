// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/platform/downloads.h"

using namespace den;

namespace {

void Write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

} // namespace

TEST_CASE("the download watch reports new wanted files once they hold still") {
    const auto dir = std::filesystem::temp_directory_path() / "den-download-watch-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    Write(dir / "old.bps", "there before");

    pal::DownloadWatch watch({dir, dir / "missing"}, [](const std::string& name) { return name.ends_with(".bps"); });
    CHECK(watch.Poll().empty()); // what was there is never reported

    Write(dir / "hack.bps", "BPS1 patch");
    Write(dir / "notes.txt", "not wanted");
    Write(dir / "empty.bps", "");
    CHECK(watch.Poll().empty()); // seen once: it may still be arriving
    auto found = watch.Poll();
    REQUIRE(found.size() == 1);
    CHECK(found[0].name == "hack.bps");
    CHECK(found[0].size == 10);
    CHECK(found[0].path == dir / "hack.bps");
    CHECK(watch.Poll().empty()); // reported once

    // A file that grows between two looks waits until it stops.
    Write(dir / "slow.bps", "half");
    CHECK(watch.Poll().empty());
    Write(dir / "slow.bps", "half and the rest");
    CHECK(watch.Poll().empty());
    found = watch.Poll();
    REQUIRE(found.size() == 1);
    CHECK(found[0].name == "slow.bps");

    // A file from before the watch that is written again counts as new.
    Write(dir / "old.bps", "downloaded again, longer");
    CHECK(watch.Poll().empty());
    found = watch.Poll();
    REQUIRE(found.size() == 1);
    CHECK(found[0].name == "old.bps");

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("only web addresses go to the browser") {
    CHECK_FALSE(pal::OpenInBrowser("file:///etc/passwd").ok);
    CHECK_FALSE(pal::OpenInBrowser("steam://run/1").ok);
    CHECK_FALSE(pal::OpenInBrowser("example.com").ok);
}
