// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/engine/host.h"
#include "den/platform/pal.h"
#include "den/store/settings.h"

using namespace den;

namespace {

struct Fixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "emuden-engine-test";
    std::unique_ptr<store::Settings> settings;
    std::unique_ptr<engine::Host> host;
    std::filesystem::path game;

    Fixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        game = dir / "game.nup";
        std::ofstream(game) << "x";
        settings = std::make_unique<store::Settings>(dir / "settings.json");
        host = engine::CreateHost({dir, settings.get()});
    }
    ~Fixture() {
        host.reset();
        settings.reset();
        std::filesystem::remove_all(dir);
    }

    // A shell stands in for the engine: `script` is what it runs.
    void UseShell(const std::string& windows_script, const std::string& posix_script) {
        const std::string comspec = GetEnv("ComSpec");
        if (!comspec.empty()) {
            settings->SetString(engine::PathKey("switch2"), comspec);
            settings->SetList(engine::ArgsKey("switch2"), {"/c", windows_script});
        } else {
            settings->SetString(engine::PathKey("switch2"), "/bin/sh");
            settings->SetList(engine::ArgsKey("switch2"), {"-c", posix_script});
        }
    }

    engine::LaunchRequest Request() const {
        engine::LaunchRequest request;
        request.system = "switch2";
        request.game_id = "game-1";
        request.title = "Game";
        request.content = game;
        return request;
    }

    std::vector<engine::Event> WaitForExit(double seconds) {
        std::vector<engine::Event> all;
        const double until = pal::NowSeconds() + seconds;
        while (pal::NowSeconds() < until) {
            for (auto& event : host->PollEvents()) {
                all.push_back(event);
            }
            if (!host->Running()) {
                break;
            }
            pal::SleepMs(20);
        }
        return all;
    }
};

} // namespace

TEST_CASE("a missing engine is reported clearly") {
    Fixture f;
    f.settings->SetString(engine::PathKey("switch2"), PathToUtf8(f.dir / "nowhere"));
    CHECK_FALSE(f.host->Available("switch2"));
    CHECK(f.host->UnavailableReason("switch2").find("engine path") != std::string::npos);
    CHECK_FALSE(f.host->Available("gba"));

    CHECK_FALSE(f.host->Start(f.Request()));
    const auto events = f.host->PollEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == engine::Event::Type::Failed);
    CHECK(events[0].game_id == "game-1");
    CHECK_FALSE(events[0].message.empty());
}

TEST_CASE("an engine folder is found by its executable") {
    Fixture f;
    const auto folder = f.dir / "engines" / "switch2";
    std::filesystem::create_directories(folder);
    f.settings->SetString(engine::PathKey("switch2"), PathToUtf8(folder));
    CHECK_FALSE(f.host->Available("switch2"));
    std::ofstream(folder / "lichii.exe") << "x";
    const auto install = f.host->Find("switch2");
    REQUIRE(install);
    CHECK(install->executable == folder / "lichii.exe");
    CHECK(install->source == "setting");
    CHECK(install->level == 0);
}

TEST_CASE("the engine program chosen on this device is looked at first") {
    Fixture f;
    const auto found = f.dir / "engines" / "switch2";
    std::filesystem::create_directories(found);
    std::ofstream(found / "lichii.exe") << "x";
    REQUIRE(f.host->Find("switch2"));
    CHECK(f.host->Find("switch2")->source == "plugin");

    const auto chosen = f.dir / "Chosen" / "engine-program";
    std::filesystem::create_directories(chosen.parent_path());
    std::ofstream(chosen) << "x";
    f.settings->SetString(engine::PathKey("switch2"), PathToUtf8(chosen));
    REQUIRE(f.host->Find("switch2"));
    CHECK(f.host->Find("switch2")->executable == chosen);
    CHECK(f.host->Find("switch2")->source == "setting");
    // Any console's: a program is a program, whatever its name.
    f.settings->SetString(engine::PathKey("otherengine"), PathToUtf8(chosen));
    REQUIRE(f.host->Find("otherengine"));
    CHECK(f.host->Find("otherengine")->executable == chosen);
    CHECK_FALSE(f.host->Find("nochoice"));
}

TEST_CASE("EmuDEN's Switch 2 engine is driven over ECP, chosen or found") {
    Fixture f;
    const auto folder = f.dir / "engines" / "switch2";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "lichii.exe") << "x";
    std::ofstream(folder / "emuden-switch2") << "x";
    REQUIRE(f.host->Find("switch2"));
    CHECK(f.host->Find("switch2")->executable == folder / "emuden-switch2");
    CHECK(f.host->Find("switch2")->level == 2);

    f.settings->SetString(engine::PathKey("switch2"), PathToUtf8(folder / "emuden-switch2"));
    REQUIRE(f.host->Find("switch2"));
    CHECK(f.host->Find("switch2")->source == "setting");
    CHECK(f.host->Find("switch2")->level == 2);
    f.settings->SetString(engine::PathKey("switch2"), PathToUtf8(folder / "lichii.exe"));
    CHECK(f.host->Find("switch2")->level == 0);
}

TEST_CASE("level 0 reports the exit and books the play time") {
    Fixture f;
    f.UseShell("ping -n 3 127.0.0.1 >nul & exit 3", "sleep 2; exit 3");
    std::string booked;
    std::int64_t booked_seconds = 0;
    engine::Hooks hooks;
    hooks.record_play = [&](const std::string& id, std::int64_t, std::int64_t seconds) {
        booked = id;
        booked_seconds = seconds;
    };
    f.host->Attach(hooks);

    REQUIRE(f.host->Start(f.Request()));
    CHECK(f.host->Running());
    CHECK_FALSE(f.host->Start(f.Request())); // one game at a time
    const auto events = f.WaitForExit(15);
    CHECK_FALSE(f.host->Running());
    REQUIRE(events.size() == 3);
    CHECK(events[0].type == engine::Event::Type::Started);
    CHECK(events[1].type == engine::Event::Type::Failed);
    CHECK(events[2].type == engine::Event::Type::Exited);
    CHECK(events[2].exit_code == 3);
    CHECK(events[2].seconds >= 1.0);
    CHECK(booked == "game-1");
    CHECK(booked_seconds >= 1);
}

TEST_CASE("stop ends an engine that does not close by itself") {
    Fixture f;
    f.UseShell("ping -n 60 127.0.0.1 >nul", "sleep 60");
    REQUIRE(f.host->Start(f.Request()));
    f.host->Stop();
    const double started = pal::NowSeconds();
    const auto events = f.WaitForExit(15);
    CHECK_FALSE(f.host->Running());
    CHECK(pal::NowSeconds() - started < 10);
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().type == engine::Event::Type::Exited);
}

TEST_CASE("destroying the host ends the engine") {
    Fixture f;
    f.UseShell("ping -n 60 127.0.0.1 >nul", "sleep 60");
    REQUIRE(f.host->Start(f.Request()));
    const double started = pal::NowSeconds();
    f.host.reset();
    CHECK(pal::NowSeconds() - started < 5);
}

// Hidden: a real engine and game, named by EMUDEN_TEST_ENGINE and EMUDEN_TEST_GAME.
TEST_CASE("a real engine starts and closes on stop", "[.real]") {
    Fixture f;
    const std::string exe = GetEnv("EMUDEN_TEST_ENGINE");
    const std::string game = GetEnv("EMUDEN_TEST_GAME");
    if (exe.empty() || game.empty()) {
        SKIP("EMUDEN_TEST_ENGINE and EMUDEN_TEST_GAME are not set");
    }
    f.settings->SetString(engine::PathKey("switch2"), exe);
    engine::LaunchRequest request = f.Request();
    request.content = PathFromUtf8(game);
    request.fullscreen = false;
    REQUIRE(f.host->Start(request));
    pal::SleepMs(15000);
    f.host->PollEvents();
    REQUIRE(f.host->Running());
    f.host->Stop();
    const double asked = pal::NowSeconds();
    f.WaitForExit(10);
    CHECK_FALSE(f.host->Running());
    WARN("closed " << pal::NowSeconds() - asked << " s after the stop");
}
