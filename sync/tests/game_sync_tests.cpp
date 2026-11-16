// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A game's own sync: the check before it starts (newer online copies come down without a
// question, files changed on both sides wait for the user), the upload when it ends, and the
// content comparison that keeps identical files from ever being a conflict.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "sync_fixture.h"

namespace {

// Two devices that synced one save: the PC and the handheld.
struct Pair {
    Cloud cloud;
    Device pc, handheld;
    explicit Pair(std::vector<std::string> categories = {"saves", "states", "config"})
        : pc(cloud, "PC", categories), handheld(cloud, "Retroid", categories) {
        pc.entries = {Game("gba:pc", "gba", pc.scratch.dir / "roms" / "Lazarus.gba")};
        handheld.entries = {Game("gba:hh", "gba", handheld.scratch.dir / "roms" / "Lazarus.gba")};
        pc.own_play["gba:pc"] = {60, 1'800'000'050, 1};
        Write(pc.Save("gba:pc"), "save 1", 1'800'000'100);
        REQUIRE(pc.Sync().ok);
        REQUIRE(handheld.Sync().ok);
        REQUIRE(pc.Sync().ok);
        REQUIRE(Read(handheld.Save("gba:hh")) == "save 1");
    }
    std::string Online() {
        std::lock_guard lock(cloud.mutex);
        return cloud.files.at("saves/gba/Lazarus.srm").data;
    }
};

GameCheck Check(Device& device, const std::string& game) {
    CheckGate gate;
    return device.engine->CheckGame(game, gate, 3);
}

} // namespace

TEST_CASE("a fetched copy decides by content whether files changed on both sides conflict") {
    const JournalEntry agreed{"base", "r1", 4, 1};
    CHECK(AfterFetch("same", "same", &agreed) == Op::Adopt);     // identical: never a conflict
    CHECK(AfterFetch("same", "same", nullptr) == Op::Adopt);     // new on both sides, the same
    CHECK(AfterFetch("mine", "base", &agreed) == Op::Upload);    // only the rev moved online
    CHECK(AfterFetch("mine", "theirs", &agreed) == Op::Conflict);
    CHECK(AfterFetch("mine", "theirs", nullptr) == Op::Conflict);
    CHECK(AfterFetch("mine", "", &agreed) == Op::Conflict);      // nothing fetched: no guess
}

TEST_CASE("before a game starts, a save changed only online comes down without a question") {
    Pair pair;
    Write(pair.pc.Save("gba:pc"), "save 2 from the PC", 1'800'000'200);
    REQUIRE(pair.pc.engine->SyncGameOnce("gba:pc", nullptr).ok);
    CHECK(pair.Online() == "save 2 from the PC");

    const GameCheck check = Check(pair.handheld, "gba:hh");
    CHECK(check.reached);
    CHECK(check.downloaded == 1);
    CHECK(check.conflicts.empty());
    CHECK(Read(pair.handheld.Save("gba:hh")) == "save 2 from the PC");
    // Nothing is left over for the next run.
    pair.cloud.puts.clear();
    REQUIRE(pair.handheld.Sync().ok);
    CHECK(pair.handheld.engine->Conflicts().empty());
    CHECK(pair.cloud.puts.empty());
}

TEST_CASE("a save changed only here waits for the game's end, then goes up at once") {
    Pair pair;
    Write(pair.handheld.Save("gba:hh"), "played offline", 1'800'000'300);
    const GameCheck check = Check(pair.handheld, "gba:hh");
    CHECK(check.reached);
    CHECK(check.waiting == 1);
    CHECK(check.conflicts.empty());
    CHECK(pair.Online() == "save 1");

    Write(pair.handheld.Save("gba:hh"), "played some more", 1'800'000'400);
    REQUIRE(pair.handheld.engine->SyncGameOnce("gba:hh", nullptr).ok);
    CHECK(pair.Online() == "played some more");
    // The other device takes it before it starts.
    CHECK(Check(pair.pc, "gba:pc").downloaded == 1);
    CHECK(Read(pair.pc.Save("gba:pc")) == "played some more");
}

TEST_CASE("a save changed on both sides asks, and the choice keeps the other copy") {
    Pair pair;
    Write(pair.pc.Save("gba:pc"), "pc's progress", 1'800'000'500);
    REQUIRE(pair.pc.engine->SyncGameOnce("gba:pc", nullptr).ok);
    Write(pair.handheld.Save("gba:hh"), "handheld's progress", 1'800'000'400);

    SECTION("the online save") {
        const GameCheck check = Check(pair.handheld, "gba:hh");
        REQUIRE(check.conflicts.size() == 1);
        CHECK(check.online_device == "PC");
        CHECK(check.online_time == 1'800'000'500);
        CHECK(check.local_time == 1'800'000'400);
        CHECK(Read(pair.handheld.Save("gba:hh")) == "handheld's progress"); // nothing replaced yet
        REQUIRE(pair.handheld.engine->ResolveGame("gba:hh", true).ok);
        CHECK(Read(pair.handheld.Save("gba:hh")) == "pc's progress");
        const auto conflicts = pair.handheld.engine->Conflicts();
        REQUIRE(conflicts.size() == 1);
        CHECK(Read(conflicts[0].file) == "handheld's progress");
        pair.cloud.puts.clear();
        REQUIRE(pair.handheld.engine->SyncGameOnce("gba:hh", nullptr).ok);
        CHECK(pair.cloud.puts.empty());
    }
    SECTION("this device's save") {
        REQUIRE(Check(pair.handheld, "gba:hh").conflicts.size() == 1);
        REQUIRE(pair.handheld.engine->ResolveGame("gba:hh", false).ok);
        CHECK(Read(pair.handheld.Save("gba:hh")) == "handheld's progress");
        const auto conflicts = pair.handheld.engine->Conflicts();
        REQUIRE(conflicts.size() == 1);
        CHECK(conflicts[0].from == "PC");
        CHECK(Read(conflicts[0].file) == "pc's progress");
        REQUIRE(pair.handheld.engine->SyncGameOnce("gba:hh", nullptr).ok);
        CHECK(pair.Online() == "handheld's progress");
        CHECK(pair.handheld.engine->Conflicts().size() == 1); // no second copy
        REQUIRE(pair.handheld.Sync().ok);
        CHECK(pair.handheld.engine->Conflicts().size() == 1);
    }
}

TEST_CASE("the same content changed on both sides is never a conflict") {
    Pair pair;
    Write(pair.pc.Save("gba:pc"), "same progress", 1'800'000'500);
    REQUIRE(pair.pc.engine->SyncGameOnce("gba:pc", nullptr).ok);
    Write(pair.handheld.Save("gba:hh"), "same progress", 1'800'000'450);
    const GameCheck check = Check(pair.handheld, "gba:hh");
    CHECK(check.conflicts.empty());
    CHECK(check.waiting == 0);
    pair.cloud.puts.clear();
    REQUIRE(pair.handheld.Sync().ok);
    CHECK(pair.handheld.engine->Conflicts().empty());
    CHECK(pair.cloud.puts.empty());
}

TEST_CASE("a rev that moved online without a change is not a conflict") {
    Pair pair;
    {
        std::lock_guard lock(pair.cloud.mutex);
        pair.cloud.files.at("saves/gba/Lazarus.srm").rev = pair.cloud.next_rev++; // renumbered
    }
    Write(pair.handheld.Save("gba:hh"), "handheld's progress", 1'800'000'400);
    REQUIRE(pair.handheld.Sync().ok);
    CHECK(pair.handheld.engine->Conflicts().empty());
    CHECK(pair.Online() == "handheld's progress");
}

TEST_CASE("a check that was given up writes nothing") {
    Pair pair;
    Write(pair.pc.Save("gba:pc"), "save 2", 1'800'000'200);
    REQUIRE(pair.pc.engine->SyncGameOnce("gba:pc", nullptr).ok);
    CheckGate gate;
    gate.Abandon();
    const GameCheck check = pair.handheld.engine->CheckGame("gba:hh", gate, 3);
    CHECK(check.downloaded == 0);
    CHECK(Read(pair.handheld.Save("gba:hh")) == "save 1");
}

TEST_CASE("a game's check does not wait for a full run's game uploads") {
    Pair pair({"saves", "states", "config", "games"});
    const fs::path rom = pair.handheld.scratch.dir / "roms" / "Big.gba";
    Write(rom, "a big game");
    pair.handheld.entries.push_back(Game("gba:big", "gba", rom));
    pair.handheld.engine->SetUploadGames(true);

    // The upload is held until the check is done; a check that waited for it would let the
    // hold run out instead.
    std::mutex mutex;
    std::condition_variable changed;
    bool uploading = false, released = false, held_out = false;
    pair.cloud.on_put = [&](const std::string& path) {
        if (path == "games/gba/Big.gba") {
            std::unique_lock lock(mutex);
            uploading = true;
            changed.notify_all();
            held_out = !changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; });
        }
    };
    // Only the hold matters here; the run's own result is checked by the other tests.
    std::thread full([&] { (void)pair.handheld.engine->RunOnce({}, nullptr); });
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return uploading; });
    }
    // The other device plays meanwhile.
    Write(pair.pc.Save("gba:pc"), "save 2", 1'800'000'200);
    REQUIRE(pair.pc.engine->SyncGameOnce("gba:pc", nullptr).ok);
    const GameCheck check = Check(pair.handheld, "gba:hh");
    {
        std::lock_guard lock(mutex);
        released = true;
    }
    changed.notify_all();
    full.join();
    CHECK(check.downloaded == 1);
    CHECK_FALSE(held_out);
    CHECK(Read(pair.handheld.Save("gba:hh")) == "save 2");
}
