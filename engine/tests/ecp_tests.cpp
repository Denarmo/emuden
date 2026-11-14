// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Emulator Control Protocol link, against an engine in the test's own process.

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/engine/host.h"
#include "den/platform/pal.h"
#include "den/store/settings.h"
#include "ecp/wire.h"
#include "fake_engine.h"

using namespace den;

namespace {

struct EcpFixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "emuden-ecp-test";
    std::unique_ptr<store::Settings> settings;
    std::unique_ptr<engine::Host> host;
    std::filesystem::path game;
    test::FakeEngine fake;
    std::vector<engine::Event> events;
    std::size_t checked = 0;

    EcpFixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        game = dir / "game.gba";
        std::ofstream(game) << "x";
        settings = std::make_unique<store::Settings>(dir / "settings.json");
        host = engine::CreateHost({dir, settings.get()});
    }
    ~EcpFixture() {
        host.reset();
        settings.reset();
        std::filesystem::remove_all(dir);
    }

    engine::LaunchRequest Request() {
        engine::LaunchRequest request;
        request.system = "gba";
        request.game_id = "game-1";
        request.title = "Game";
        request.content = game;
        request.settings = {{"video.internal_scale", "3"}, {"video.widescreen_patches", "true"}, {"no.such", "1"}};
        engine::Install install;
        install.level = 2;
        install.core_id = "fake";
        install.source = "manifest";
        install.attach = [this](int port, const std::string& token) { fake.Connect(port, token); };
        request.install = install;
        return request;
    }

    // Pumps until an event of the type arrived (true), or `seconds` passed.
    bool PumpUntil(engine::Event::Type type, double seconds = 5) {
        const double until = pal::NowSeconds() + seconds;
        // Events seen on an earlier call count too, once.
        for (std::size_t i = checked; i < events.size(); ++i) {
            if (events[i].type == type) {
                checked = i + 1;
                return true;
            }
        }
        checked = events.size();
        while (pal::NowSeconds() < until) {
            bool found = false;
            for (engine::Event& event : host->PollEvents()) {
                if (!found && event.type == type) {
                    found = true;
                    checked = events.size() + 1;
                }
                events.push_back(std::move(event));
            }
            if (found) {
                return true;
            }
            pal::SleepMs(5);
        }
        checked = events.size();
        return false;
    }
};

} // namespace

TEST_CASE("ecp frames and codecs", "[engine][ecp]") {
    const std::string text = "any carnal pleasure.";
    const std::string encoded = engine::ecp::Base64Encode(text.data(), text.size());
    CHECK(encoded == "YW55IGNhcm5hbCBwbGVhc3VyZS4=");
    const auto decoded = engine::ecp::Base64Decode(encoded);
    REQUIRE(decoded);
    CHECK(std::string(decoded->begin(), decoded->end()) == text);
    CHECK_FALSE(engine::ecp::Base64Decode("a$bc"));

    CHECK(engine::ecp::NewToken().size() == 64);
    CHECK(engine::ecp::SameToken("abc", "abc"));
    CHECK_FALSE(engine::ecp::SameToken("abc", "abd"));
    CHECK_FALSE(engine::ecp::SameToken("abc", "abcd"));

    const JsonValue message = engine::ecp::Message("set_setting", 7, {{"key", JsonString("a\"b")}, {"value", JsonNumber(2.5)}});
    const std::string expected = "{\"type\":\"set_setting\",\"id\":7,\"key\":\"a\\\"b\",\"value\":2.5}";
    CHECK(engine::ecp::CompactJson(message) == expected);

    engine::SettingDef toggle;
    toggle.type = "toggle";
    CHECK(engine::ecp::TypedValue(&toggle, "true").kind == JsonValue::Kind::Bool);
    engine::SettingDef range;
    range.type = "range";
    CHECK(engine::ecp::TypedValue(&range, "3").number == 3);
    CHECK(engine::ecp::TypedValue(nullptr, "3").kind == JsonValue::Kind::String);
}

TEST_CASE("an ecp engine is launched, paused, saved and stopped", "[engine][ecp]") {
    EcpFixture f;
    REQUIRE(f.host->Start(f.Request()));
    REQUIRE(f.PumpUntil(engine::Event::Type::FirstFrame));
    engine::Control* control = f.host->Control();
    REQUIRE(control != nullptr);
    CHECK(control->Level() == 2);
    CHECK(control->Has("states"));
    CHECK(control->Has("menu"));
    CHECK_FALSE(control->Has("cheats")); // EmuDEN does not drive it, so it is not used
    CHECK(control->Ready());
    CHECK(control->Playing());

    // The frontend's hello says what it uses; the launch carries the typed settings the
    // engine has, and EmuDEN's way into its menu.
    const JsonValue hello = f.fake.Last("hello");
    CHECK(hello.Find("level")->number == 2);
    const JsonValue launch = f.fake.Last("launch");
    CHECK(launch.String("game_id") == "game-1");
    CHECK(launch.String("content") == PathToUtf8(f.game));
    const JsonValue* settings = launch.Find("settings");
    REQUIRE(settings != nullptr);
    CHECK(settings->Find("video.internal_scale")->number == 3);
    CHECK(settings->Find("video.widescreen_patches")->boolean);
    CHECK(settings->Find("no.such") == nullptr);
    CHECK(launch.Find("menu_chord")->items.size() == 2);

    REQUIRE(control->Schema().size() == 3);
    CHECK(control->Schema()[0].key == "video.internal_scale");
    CHECK(control->Schema()[2].choices.size() == 2);

    // Perf arrived with the first frame.
    f.PumpUntil(engine::Event::Type::Exited, 0.1);
    CHECK(control->GetPerf().valid);
    CHECK(control->GetPerf().fps > 59);

    control->Pause();
    REQUIRE(f.PumpUntil(engine::Event::Type::Paused));
    CHECK(control->Paused());

    const auto saved = control->SaveState(3);
    REQUIRE(saved);
    CHECK(saved.value().slot == 3);
    CHECK(saved.value().time == 1700000000);
    CHECK_FALSE(saved.value().thumbnail.Empty());
    CHECK(control->LoadState(3));
    const Status empty = control->LoadState(4);
    CHECK_FALSE(empty);
    CHECK(empty.error == "That slot is empty.");
    const auto states = control->ListStates();
    REQUIRE(states);
    CHECK(states.value().size() == 1);

    control->SetSetting("video.internal_scale", "2");
    REQUIRE(f.fake.WaitFor("set_setting"));
    CHECK(f.fake.Last("set_setting").Find("value")->number == 2);
    control->SetSpeed(true, 3);
    REQUIRE(f.fake.WaitFor("set_speed"));
    CHECK(f.fake.Last("set_speed").Find("factor")->number == 3);

    // The engine hands the menu over with its paused frame.
    f.fake.SendEvent(JsonObject({{"type", JsonString("menu_request")},
                                 {"frame", JsonObject({{"png", JsonString(test::TINY_PNG)},
                                                       {"width", JsonNumber(1)},
                                                       {"height", JsonNumber(1)}})}}));
    REQUIRE(f.PumpUntil(engine::Event::Type::MenuRequested));
    const auto frame = control->TakeMenuRequest();
    REQUIRE(frame);
    CHECK(frame->width == 1);
    CHECK_FALSE(control->TakeMenuRequest());

    control->Resume();
    REQUIRE(f.PumpUntil(engine::Event::Type::Resumed));
    CHECK_FALSE(control->Paused());

    f.host->Stop();
    REQUIRE(f.PumpUntil(engine::Event::Type::Exited));
    CHECK(f.fake.Got("stop"));
    CHECK_FALSE(f.host->Running());
    CHECK(f.host->Control() == nullptr);
}

TEST_CASE("an ecp engine with the wrong token is turned away", "[engine][ecp]") {
    EcpFixture f;
    f.fake.token_override = std::string(64, '0');
    REQUIRE(f.host->Start(f.Request()));
    REQUIRE(f.PumpUntil(engine::Event::Type::Failed));
    REQUIRE(f.PumpUntil(engine::Event::Type::Exited));
    CHECK_FALSE(f.fake.Got("launch"));
}

TEST_CASE("an ecp launch failure is the engine's typed error", "[engine][ecp]") {
    EcpFixture f;
    f.fake.launch_error = "bios_missing";
    REQUIRE(f.host->Start(f.Request()));
    REQUIRE(f.PumpUntil(engine::Event::Type::Failed));
    CHECK(f.events[f.checked - 1].message == "The engine needs a BIOS that is not installed.");
    f.host->Stop();
    CHECK(f.PumpUntil(engine::Event::Type::Exited));
}

TEST_CASE("a level-1 ecp engine gets no level-2 requests", "[engine][ecp]") {
    EcpFixture f;
    f.fake.level = 1;
    REQUIRE(f.host->Start(f.Request()));
    REQUIRE(f.PumpUntil(engine::Event::Type::FirstFrame));
    engine::Control* control = f.host->Control();
    REQUIRE(control != nullptr);
    CHECK(control->Level() == 1);
    CHECK_FALSE(control->Has("pause"));
    CHECK(f.fake.Last("launch").Find("settings") == nullptr);
    CHECK_FALSE(f.fake.Got("get_schema"));
    control->Pause();
    CHECK_FALSE(control->SaveState(1));
    f.host->Stop();
    REQUIRE(f.PumpUntil(engine::Event::Type::Exited));
    CHECK_FALSE(f.fake.Got("pause"));
}
