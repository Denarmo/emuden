// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Home's Continue: the games last played on any device, and the resume state and its picture
// that follow a session from one device to the next.

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "den/sync/continue.h"
#include "sync_fixture.h"

namespace {

fs::path StateOf(const Device& device, const std::string& id, const char* suffix) {
    return device.scratch.dir / "states" / "gba" / (SafeFileName(id) + suffix);
}

ContinueRequest RequestFor(const Device& device) {
    ContinueRequest request;
    request.data_dir = device.scratch.dir;
    request.own_id = device.settings->GetString("sync.device_id");
    request.entries = device.entries;
    request.own_play = device.own_play;
    return request;
}

void PlayFile(const fs::path& data, const std::string& id, const std::string& device, const std::string& games) {
    Write(data / "sync" / "playtime" / (id + ".json"),
          R"({"version": 1, "device": ")" + device + R"(", "games": {)" + games + "}}");
}

} // namespace

TEST_CASE("Continue lists the newest plays of every device, each game once") {
    Scratch scratch;
    std::vector<library::Entry> entries = {Game("gba:a", "gba", scratch.dir / "roms" / "Lazarus.gba"),
                                           Game("gb:b", "gb", scratch.dir / "roms" / "Tetris.gb"),
                                           Game("pc:s", "steam", scratch.dir / "steam" / "Portal.acf")};
    ContinueRequest request;
    request.data_dir = scratch.dir;
    request.own_id = "0000000000000001";
    request.entries = entries;
    request.own_play = {{"gba:a", {60, 1000, 1}}, {"gb:b", {60, 3000, 1}}, {"pc:s", {60, 500, 1}}};
    request.never = {"steam"};
    PlayFile(scratch.dir, "0000000000000001", "Myself", R"("gba/Lazarus": {"last_played": 99999})");
    PlayFile(scratch.dir, "00000000000000aa", "Legion Go S",
             R"("gba/lazarus": {"seconds": 5, "count": 1, "last_played": 2000},
                "snes/Chrono Trigger": {"seconds": 5, "count": 1, "last_played": 4000},
                "steam/Portal": {"last_played": 9000})");

    const std::vector<ContinueItem> items = ContinueGames(request);
    REQUIRE(items.size() == 4);
    // Only another device has it: offered by name, for a download.
    CHECK(items[0].key == "snes/Chrono Trigger");
    CHECK(items[0].game_id.empty());
    CHECK(items[0].device == "Legion Go S");
    CHECK(items[1].game_id == "gb:b");
    CHECK(items[1].device.empty());
    // The other device's later play wins over this one's (its own file is never read here).
    CHECK(items[2].game_id == "gba:a");
    CHECK(items[2].device == "Legion Go S");
    CHECK(items[2].time == 2000);
    // A PC game counts only as this device's own.
    CHECK(items[3].game_id == "pc:s");
    CHECK(items[3].time == 500);

    request.limit = 2;
    CHECK(ContinueGames(request).size() == 2);
}

TEST_CASE("Continue takes the resume state's time and picture from that session only") {
    Scratch scratch;
    ContinueRequest request;
    request.data_dir = scratch.dir;
    request.entries = {Game("gba:a", "gba", scratch.dir / "roms" / "Lazarus.gba")};
    request.own_play = {{"gba:a", {600, 1'800'000'000, 1}}};
    const fs::path state = ResumeState(scratch.dir, "gba", "gba:a");
    const fs::path picture = ResumeScreenshot(scratch.dir, "gba", "gba:a");
    Write(picture, "png");

    SECTION("written as the session ended") {
        Write(state, "state", 1'800'000'600);
        const auto items = ContinueGames(request);
        REQUIRE(items.size() == 1);
        CHECK(items[0].resume);
        CHECK(items[0].time == 1'800'000'600);
        CHECK(items[0].screenshot == picture);
    }
    SECTION("left from an older session") {
        Write(state, "state", 1'700'000'000);
        const auto items = ContinueGames(request);
        REQUIRE(items.size() == 1);
        CHECK(items[0].resume);
        CHECK(items[0].time == 1'800'000'000);
        CHECK(items[0].screenshot.empty());
    }
}

TEST_CASE("a session's resume state and picture go up before its play time, and come down on the other device") {
    Cloud cloud;
    Device legion(cloud, "Legion Go S", {"saves", "states", "config"});
    Device retroid(cloud, "Retroid", {"saves", "states", "config"});
    legion.entries = {Game("gba:l", "gba", legion.scratch.dir / "roms" / "Lazarus.gba")};
    retroid.entries = {Game("gba:r", "gba", retroid.scratch.dir / "roms" / "Lazarus.gba")};
    REQUIRE(legion.Sync().ok);
    REQUIRE(retroid.Sync().ok);

    // The Legion plays and leaves: the player wrote the resume state and its picture.
    legion.own_play["gba:l"] = {300, 1'800'000'000, 1};
    Write(StateOf(legion, "gba:l", ".resume.state"), "the moment it was left", 1'800'000'300);
    Write(StateOf(legion, "gba:l", ".resume.png"), "its picture", 1'800'000'300);
    cloud.puts.clear();
    REQUIRE(legion.engine->SyncGameOnce("gba:l", nullptr).ok);
    const auto at = [&](const std::string& path) {
        return std::find(cloud.puts.begin(), cloud.puts.end(), path) - cloud.puts.begin();
    };
    const auto play = std::find_if(cloud.puts.begin(), cloud.puts.end(),
                                   [](const std::string& p) { return p.starts_with("config/playtime/"); });
    REQUIRE(play != cloud.puts.end());
    CHECK(at("states/gba/Lazarus.resume.state") < play - cloud.puts.begin());
    CHECK(at("states/gba/Lazarus.resume.png") < play - cloud.puts.begin());
    CHECK(legion.engine->GameProgress(false).game_id.empty()); // nothing goes on any more

    // The Retroid's start: its run brings the play time file and the states.
    REQUIRE(retroid.Sync().ok);
    std::vector<ContinueItem> items = ContinueGames(RequestFor(retroid));
    REQUIRE(items.size() == 1);
    CHECK(items[0].game_id == "gba:r");
    CHECK(items[0].device == "Legion Go S");
    CHECK(items[0].time == 1'800'000'300);
    CHECK(Read(items[0].screenshot) == "its picture");

    // Played once more on the Legion: Continue on the Retroid takes it before the game starts.
    legion.own_play["gba:l"] = {600, 1'800'001'000, 2};
    Write(StateOf(legion, "gba:l", ".resume.state"), "a later moment", 1'800'001'300);
    REQUIRE(legion.engine->SyncGameOnce("gba:l", nullptr).ok);
    CheckGate gate;
    const GameCheck check = retroid.engine->CheckGame("gba:r", gate, 3);
    CHECK(check.conflicts.empty());
    CHECK(check.downloaded == 1);
    CHECK(Read(StateOf(retroid, "gba:r", ".resume.state")) == "a later moment");
    CHECK(retroid.engine->GameProgress(true).game_id.empty());
}

TEST_CASE("how long ago, in words") {
    CHECK(AgoText(1000, 1030) == "Just now");
    CHECK(AgoText(1000, 1000 + 5 * 60) == "5 min ago");
    CHECK(AgoText(0, 3 * 3600 + 10) == "3 h ago");
    CHECK(AgoText(0, 30 * 3600) == "Yesterday");
    CHECK(AgoText(0, 4 * 86400) == "4 days ago");
    CHECK(AgoText(0, 21 * 86400) == "3 weeks ago");
    CHECK(AgoText(0, 90 * 86400) == "3 months ago");
    CHECK(AgoText(2000, 1000) == "Just now"); // a clock a little ahead
}
