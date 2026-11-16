// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The player session over an engine app, against an engine in the test's own process.

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/engine/host.h"
#include "den/platform/pal.h"
#include "den/player/session.h"
#include "den/store/settings.h"
#include "fake_engine.h"

using namespace den;

namespace {

struct EngineFixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "emuden-engine-session-test";
    std::unique_ptr<store::Settings> settings;
    std::unique_ptr<engine::Host> host;
    std::unique_ptr<player::Session> session;
    test::FakeEngine fake;
    std::filesystem::path game;

    EngineFixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        game = dir / "game.gba";
        std::ofstream(game) << "x";
        settings = std::make_unique<store::Settings>(dir / "settings.json");
        host = engine::CreateHost({dir, settings.get()});
        player::Config config;
        config.data_dir = dir;
        config.settings = settings.get();
        session = player::CreateRoutedSession(player::CreateSession(config),
                                              player::CreateEngineSession(config, *host));
    }
    ~EngineFixture() {
        session.reset();
        host.reset();
        settings.reset();
        std::filesystem::remove_all(dir);
    }

    player::OpenRequest Request() {
        player::OpenRequest request;
        request.content = game;
        request.game_id = "game-1";
        request.system = "gba";
        request.title = "Game";
        request.option_hints = {{"video.aspect", "16:9"}};
        engine::Install install;
        install.level = 2;
        install.core_id = "fake";
        install.attach = [this](int port, const std::string& token) { fake.Connect(port, token); };
        request.engine = install;
        return request;
    }

    // Advances the session until `done` holds or `seconds` passed.
    template <typename F>
    bool AdvanceUntil(F done, double seconds = 5) {
        const double until = pal::NowSeconds() + seconds;
        while (pal::NowSeconds() < until) {
            host->Pump();
            session->Advance(0.016);
            if (done()) {
                return true;
            }
            pal::SleepMs(5);
        }
        return false;
    }

    bool Ready() {
        return AdvanceUntil([&] { return session->GetSnapshot().can_save_states && fake.Got("list_states"); });
    }
};

} // namespace

TEST_CASE("an engine game is driven through the player session", "[player][engine]") {
    EngineFixture f;
    f.settings->SetList("engine_options.fake", {"video.internal_scale=2"});
    f.settings->SetList("engine_game_options.gba.game-1", {"video.internal_scale=4"});
    REQUIRE(f.session->Open(f.Request()));
    REQUIRE(f.Ready());

    // The launch carries the manifest's hints and the user's values, the game's last.
    const JsonValue launch = f.fake.Last("launch");
    const JsonValue* settings = launch.Find("settings");
    REQUIRE(settings != nullptr);
    REQUIRE(settings->Find("video.aspect") != nullptr);
    REQUIRE(settings->Find("video.internal_scale") != nullptr);
    CHECK(settings->Find("video.aspect")->string == "16:9");
    CHECK(settings->Find("video.internal_scale")->number == 4);

    player::Snapshot snapshot = f.session->GetSnapshot();
    CHECK(snapshot.state == player::State::Running);
    CHECK(snapshot.external);
    CHECK_FALSE(snapshot.can_picture);
    CHECK(snapshot.can_pause);
    CHECK(snapshot.can_fast_forward);
    CHECK(snapshot.can_reset);
    CHECK(snapshot.can_options);
    CHECK(snapshot.can_volume);
    CHECK_FALSE(snapshot.can_rewind);
    CHECK(snapshot.game_id == "game-1");

    // The engine's schema is the options page; toggles and ranges get their values.
    const auto options = f.session->Options();
    REQUIRE(options.size() == 3);
    CHECK(options[0].key == "video.internal_scale");
    CHECK(options[0].value == "4");
    CHECK(options[0].for_game);
    CHECK(options[0].values == std::vector<std::string>{"1", "2", "3", "4"});
    CHECK(options[1].values == std::vector<std::string>{"false", "true"});

    f.session->SetOption("video.internal_scale", "3", player::OptionScope::Console);
    REQUIRE(f.fake.WaitFor("set_setting"));
    CHECK(f.settings->GetList("engine_options.fake") == std::vector<std::string>{"video.internal_scale=3"});
    CHECK(f.settings->GetList("engine_game_options.gba.game-1").empty());

    // The menu: pause, a state in the player's first slot, load it, resume.
    f.session->Pause();
    REQUIRE(f.fake.WaitFor("pause"));
    CHECK(f.session->GetSnapshot().state == player::State::Paused);
    CHECK_FALSE(f.session->HasState(0));
    REQUIRE(f.session->SaveState(0));
    CHECK(f.fake.Last("save_state").Find("slot")->number == 1);
    CHECK(f.session->HasState(0));
    CHECK(IsRegularFile(f.session->StateThumbnail(0)));
    REQUIRE(f.session->LoadState(0));
    CHECK_FALSE(f.session->LoadState(5));
    f.session->SetFastForward(true);
    REQUIRE(f.fake.WaitFor("set_speed"));
    f.session->SetVolume(0.5f);
    REQUIRE(f.fake.WaitFor("set_volume"));
    CHECK(f.fake.Last("set_volume").Find("percent")->number == 50);
    f.session->Resume();
    REQUIRE(f.fake.WaitFor("resume"));
    CHECK(f.session->GetSnapshot().state == player::State::Running);

    // The engine hands its menu over: the player screen hears of it once.
    f.fake.SendEvent(JsonObject({{"type", JsonString("menu_request")},
                                 {"frame", JsonObject({{"png", JsonString(test::TINY_PNG)},
                                                       {"width", JsonNumber(1)},
                                                       {"height", JsonNumber(1)}})}}));
    REQUIRE(f.AdvanceUntil([&] { return f.session->TakeMenuRequest(); }));
    CHECK_FALSE(f.session->TakeMenuRequest());
    CHECK(f.session->GetSnapshot().state == player::State::Paused);

    // Closing writes the resume state (the engine's slot 0) and stops the engine.
    f.session->Close();
    CHECK(f.fake.Last("save_state").Find("slot")->number == 0);
    CHECK(f.session->GetSnapshot().state == player::State::Closed);
    REQUIRE(f.AdvanceUntil([&] { return !f.host->Running(); }));
    CHECK(f.fake.Got("stop"));
}

TEST_CASE("an engine that fails to launch fails the session", "[player][engine]") {
    EngineFixture f;
    f.fake.launch_error = "keys_missing";
    REQUIRE(f.session->Open(f.Request()));
    REQUIRE(f.AdvanceUntil([&] { return f.session->GetSnapshot().state == player::State::Failed; }));
    CHECK(f.session->GetSnapshot().error == "The engine needs keys that are not installed.");
}

TEST_CASE("an engine session resumes from its resume state", "[player][engine]") {
    EngineFixture f;
    f.fake.Seed(0);
    REQUIRE(f.session->Open(f.Request()));
    REQUIRE(f.AdvanceUntil([&] { return f.session->GetSnapshot().resumed; }));
    CHECK(f.fake.Last("load_state").Find("slot")->number == 0);
    CHECK_FALSE(f.session->HasState(0)); // the resume slot is not one of the player's
    f.session->Close();
    REQUIRE(f.AdvanceUntil([&] { return !f.host->Running(); }));
}

TEST_CASE("an engine game started again does not hear the last session's app", "[player][engine]") {
    EngineFixture f;
    REQUIRE(f.session->Open(f.Request()));
    REQUIRE(f.Ready());
    f.fake.SendEvent(JsonObject({{"type", JsonString("menu_request")}}));
    for (int i = 0; i < 40; ++i) {
        f.host->Pump();
        pal::SleepMs(5);
    }
    // The player screen left: the app's last words and its exit reach the host, not the session.
    f.session->Close();
    const double until = pal::NowSeconds() + 5;
    while (f.host->Running() && pal::NowSeconds() < until) {
        f.host->Pump();
        pal::SleepMs(5);
    }
    REQUIRE_FALSE(f.host->Running());

    test::FakeEngine again;
    player::OpenRequest request = f.Request();
    request.engine->attach = [&again](int port, const std::string& token) { again.Connect(port, token); };
    REQUIRE(f.session->Open(request));
    REQUIRE(f.AdvanceUntil([&] { return f.session->GetSnapshot().can_save_states && again.Got("list_states"); }));
    CHECK(f.session->GetSnapshot().state == player::State::Running);
    CHECK_FALSE(f.session->TakeMenuRequest());
    f.session->Close();
    REQUIRE(f.AdvanceUntil([&] { return !f.host->Running(); }));
}

TEST_CASE("a console without states or fast-forward does not offer them", "[player][engine]") {
    EngineFixture f;
    f.fake.Seed(0);
    player::OpenRequest request = f.Request();
    request.allowed.save_states = false;
    request.allowed.fast_forward = false;
    REQUIRE(f.session->Open(request));
    REQUIRE(f.AdvanceUntil([&] { return f.fake.Got("list_states"); }));
    const player::Snapshot snapshot = f.session->GetSnapshot();
    CHECK_FALSE(snapshot.can_save_states);
    CHECK_FALSE(snapshot.can_fast_forward);
    CHECK_FALSE(snapshot.resumed); // the resume state is a state too
    CHECK_FALSE(f.session->SaveState(0));
    f.session->Close();
    CHECK_FALSE(f.fake.Got("save_state"));
    CHECK_FALSE(f.fake.Got("load_state"));
    REQUIRE(f.AdvanceUntil([&] { return !f.host->Running(); }));
}
