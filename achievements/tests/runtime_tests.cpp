// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "den/achievements/games.h"
#include "den/achievements/runtime.h"
#include "fake_server.h"

using namespace den;
using namespace den::achievements;
using den::achievements::test::FakeServer;

namespace {

constexpr std::uint32_t NES = 7;

struct Rig {
    FakeServer server;
    std::array<std::uint8_t, 0x800> ram{};
    std::unique_ptr<Runtime> runtime;

    Rig() {
        server.answers["login2"] = {200, test::LOGIN_OK};
        server.answers["achievementsets"] = {200, test::GAME_DATA};
        server.answers["startsession"] = {200, test::NO_UNLOCKS};
        server.answers["awardachievement"] = {200, test::AWARD_OK};
        server.answers["submitlbentry"] = {200, R"({"Success":true,"Response":{"Score":7,"BestScore":7,"RankInfo":{"Rank":1,"NumEntries":"1"},"TopEntries":[]}})"};
        server.answers["ping"] = {200, R"({"Success":true})"};
        Config config;
        config.http = server.Fn();
        config.spawn = [](std::function<void()> work) { work(); }; // answers wait for Pump
        runtime = std::make_unique<Runtime>(config);
    }

    GameMemory Memory() {
        GameMemory memory;
        memory.region = [this](unsigned id) {
            // RETRO_MEMORY_SYSTEM_RAM is 2; nothing else is shown.
            return id == 2 ? std::span<std::uint8_t>(ram) : std::span<std::uint8_t>();
        };
        return memory;
    }

    void SignIn() {
        bool done = false;
        runtime->LoginWithPassword("Tester", "Pa$$word", [&](Result<Credentials> got) {
            REQUIRE(got.ok());
            CHECK(got->token == "ApiToken");
            CHECK(got->user == "Tester");
            done = true;
        });
        runtime->Pump();
        REQUIRE(done);
    }

    Status Load() {
        Status result = Status::Fail("not answered");
        runtime->LoadGame(NES, "0123456789abcdef0123456789abcdef", Memory(), [&](Status status) { result = status; });
        for (int i = 0; i < 4; ++i) {
            runtime->Pump();
        }
        return result;
    }

    std::vector<Event> Frames(int count) {
        std::vector<Event> all;
        for (int i = 0; i < count; ++i) {
            runtime->DoFrame();
            runtime->Pump();
            for (Event& event : runtime->TakeEvents()) {
                all.push_back(std::move(event));
            }
        }
        return all;
    }
};

bool Has(const std::vector<Event>& events, EventKind kind) {
    for (const Event& event : events) {
        if (event.kind == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("systems map to RetroAchievements consoles") {
    CHECK(ConsoleFor("gb") == 4);
    CHECK(ConsoleFor("gbc") == 6);
    CHECK(ConsoleFor("gba") == 5);
    CHECK(ConsoleFor("nds") == 18);
    CHECK(ConsoleFor("psx") == 12);
    CHECK(ConsoleFor("ps2") == 21);
    CHECK(ConsoleFor("wsc") == 53);
    CHECK(ConsoleFor("switch2") == 0);
    CHECK(ConsoleFor("pico8") == 0);
}

TEST_CASE("the user agent names the app and rcheevos") {
    const std::string agent = UserAgent("EmuDEN", "0.1.0");
    CHECK(agent.rfind("EmuDEN/0.1.0 rcheevos/", 0) == 0);
}

TEST_CASE("sign-in with a password gives the token and keeps no password") {
    Rig rig;
    rig.SignIn();
    CHECK(rig.runtime->SignedIn());
    const auto user = rig.runtime->UserInfo();
    REQUIRE(user);
    CHECK(user->name == "Tester");
    CHECK(user->score == 12345);
    CHECK(user->score_softcore == 123);
    REQUIRE(rig.server.Count("login2") == 1);
    CHECK(rig.server.asked[0].post_data.find("p=Pa%24%24word") != std::string::npos);

    rig.runtime->Logout();
    CHECK_FALSE(rig.runtime->SignedIn());
    // A token sign-in sends the token, not a password.
    bool ok = false;
    rig.runtime->LoginWithToken({"Tester", "ApiToken"}, [&](Status status) { ok = status.ok; });
    rig.runtime->Pump();
    CHECK(ok);
    CHECK(rig.server.asked.back().post_data.find("t=ApiToken") != std::string::npos);
    CHECK(rig.server.asked.back().post_data.find("p=") == std::string::npos);
}

TEST_CASE("a wrong password is told") {
    Rig rig;
    rig.server.answers["login2"] = {401, R"({"Success":false,"Error":"Invalid User/Password combination. Please try again","Code":"invalid_credentials","Status":401})"};
    std::string why;
    rig.runtime->LoginWithPassword("Tester", "nope", [&](Result<Credentials> got) {
        CHECK_FALSE(got.ok());
        why = got.error();
    });
    rig.runtime->Pump();
    CHECK(why.find("Invalid User/Password") != std::string::npos);
    CHECK_FALSE(rig.runtime->SignedIn());
}

TEST_CASE("an achievement unlocks when the game's memory matches, once") {
    Rig rig;
    rig.SignIn();
    REQUIRE(rig.Load().ok);
    CHECK(rig.runtime->GameState() == LoadState::Loaded);
    auto progress = rig.runtime->Progress();
    REQUIRE(progress);
    CHECK(progress->title == "Sample Game");
    CHECK(progress->total == 2);
    CHECK(progress->unlocked == 0);
    CHECK(progress->points_total == 15);

    rig.Frames(2); // the conditions are false first: the achievements arm
    rig.ram[1] = 3;
    rig.ram[2] = 7;
    const std::vector<Event> events = rig.Frames(2);
    REQUIRE(Has(events, EventKind::Unlocked));
    for (const Event& event : events) {
        if (event.kind == EventKind::Unlocked) {
            CHECK(event.id == 5501);
            CHECK(event.title == "First steps");
            CHECK(event.points == 5);
            CHECK_FALSE(event.hardcore);
        }
    }
    CHECK(rig.server.Count("awardachievement") == 1);
    CHECK(rig.server.asked.back().post_data.find("a=5501") != std::string::npos);
    CHECK(rig.server.asked.back().post_data.find("h=0") != std::string::npos);

    progress = rig.runtime->Progress();
    REQUIRE(progress);
    CHECK(progress->unlocked == 1);
    CHECK(progress->points_unlocked == 5);
    CHECK(progress->achievements.front().id == 5502); // locked ones first
    CHECK(progress->achievements.back().unlocked);
    CHECK(rig.runtime->RichPresence() == "Level 3");

    // Still true: no second unlock.
    CHECK_FALSE(Has(rig.Frames(3), EventKind::Unlocked));
    CHECK(rig.server.Count("awardachievement") == 1);
}

TEST_CASE("a leaderboard attempt starts and is cancelled, in hardcore only") {
    Rig rig;
    rig.SignIn();
    REQUIRE(rig.Load().ok);
    rig.Frames(2);
    rig.ram[0x0C] = 1;
    CHECK_FALSE(Has(rig.Frames(1), EventKind::LeaderboardStarted)); // softcore runs no leaderboards
    rig.ram[0x0C] = 0;
    rig.runtime->UnloadGame();

    rig.runtime->SetHardcore(true);
    REQUIRE(rig.Load().ok);
    rig.Frames(2);
    rig.ram[0x0C] = 1;
    CHECK(Has(rig.Frames(1), EventKind::LeaderboardStarted));
    rig.ram[0x0C] = 0;
    rig.ram[0x0D] = 1;
    CHECK(Has(rig.Frames(1), EventKind::LeaderboardFailed));
}

TEST_CASE("a game the server does not know is told apart from a failure") {
    Rig rig;
    rig.server.answers["achievementsets"] = {404, test::NOT_FOUND};
    rig.SignIn();
    const Status loaded = rig.Load();
    CHECK_FALSE(loaded.ok);
    CHECK(rig.runtime->GameState() == LoadState::Unknown);
    CHECK_FALSE(rig.runtime->Progress());
    rig.Frames(2); // nothing to check, nothing breaks
}

TEST_CASE("turning hardcore on while playing asks for a reset and unlocks count as hardcore") {
    Rig rig;
    rig.SignIn();
    REQUIRE(rig.Load().ok);
    CHECK_FALSE(rig.runtime->Hardcore());
    rig.runtime->SetHardcore(true);
    CHECK(rig.runtime->Hardcore());
    std::vector<Event> events = rig.runtime->TakeEvents();
    for (int i = 0; i < 4; ++i) {
        rig.runtime->Pump();
    }
    for (Event& event : rig.Frames(2)) {
        events.push_back(std::move(event));
    }
    CHECK(Has(events, EventKind::ResetWanted));
    // Nothing is checked until the game was reset.
    rig.ram[1] = 3;
    rig.ram[2] = 7;
    CHECK_FALSE(Has(rig.Frames(2), EventKind::Unlocked));
    rig.ram[1] = 0;
    rig.ram[2] = 0;
    rig.runtime->Reset();
    rig.Frames(2);
    rig.ram[1] = 3;
    rig.ram[2] = 7;
    events = rig.Frames(2);
    REQUIRE(Has(events, EventKind::Unlocked));
    CHECK(events.front().hardcore);
    CHECK(rig.server.asked.back().post_data.find("h=1") != std::string::npos);
}

TEST_CASE("a loaded state starts what was counted again") {
    Rig rig;
    rig.SignIn();
    REQUIRE(rig.Load().ok);
    rig.Frames(2);
    rig.runtime->StateLoaded();
    rig.runtime->Reset();
    rig.ram[1] = 3;
    rig.ram[2] = 7;
    // After a state the conditions must be seen false once again before an unlock.
    CHECK_FALSE(Has(rig.Frames(2), EventKind::Unlocked));
    rig.ram[1] = 0;
    rig.Frames(1);
    rig.ram[1] = 3;
    CHECK(Has(rig.Frames(2), EventKind::Unlocked));
}

TEST_CASE("pages outside a game resolve a hash and fetch the user's unlocks") {
    FakeServer server;
    server.answers["gameid"] = {200, R"({"Success":true,"GameID":1234})"};
    server.answers["achievementsets"] = {200, test::GAME_DATA};
    server.answers["unlocks"] = {200, R"({"Success":true,"UserUnlocks":[5502],"GameID":1234,"HardcoreMode":false})"};
    const HttpFn http = server.Fn();
    const auto id = ResolveHash(http, {}, "0123456789abcdef0123456789abcdef");
    REQUIRE(id.ok());
    CHECK(id.value() == 1234);

    const auto game = FetchProgress(http, {}, {"Tester", "ApiToken"}, "0123456789abcdef0123456789abcdef", false);
    REQUIRE(game.ok());
    CHECK(game.value().id == 1234);
    CHECK(game.value().total == 2);
    CHECK(game.value().unlocked == 1);
    CHECK(game.value().points_unlocked == 10);
    CHECK_FALSE(game.value().achievements.front().unlocked);
    CHECK(game.value().achievements.back().id == 5502);

    server.answers["achievementsets"] = {404, test::NOT_FOUND};
    const auto unknown = FetchProgress(http, {}, {"Tester", "ApiToken"}, "ffff", false);
    REQUIRE(unknown.ok());
    CHECK(unknown.value().id == 0);
}
