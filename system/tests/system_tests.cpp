// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/store/settings.h"
#include "den/system/first_run.h"
#include "den/system/info.h"
#include "den/system/settings.h"

using namespace den;
using system::Layer;
using system::Scope;

namespace {

const system::Key<bool> REWIND{"test.rewind", true, {.label = "Rewind", .section = "Test", .per_game = true}};
const system::Key<int> VOLUME{"test.volume", 80, {.label = "Volume", .section = "Test", .max = 100, .step = 5}};
const system::Key<std::string> FILTER{
    "test.filter", "sharp", {.label = "Filter", .section = "Test", .options = {"sharp", "smooth"}}};

std::filesystem::path FreshDir() {
    const auto dir = std::filesystem::temp_directory_path() / "emuden-system-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("keys register with their kind and section") {
    const system::KeyInfo* info = system::FindKey("test.filter");
    REQUIRE(info);
    CHECK(info->kind == system::Kind::Choice);
    CHECK(system::FindKey("test.rewind")->kind == system::Kind::Bool);
    CHECK(system::FindKey("test.volume")->kind == system::Kind::Number);
    CHECK(system::KeysIn("Test").size() == 3);
}

TEST_CASE("a value is resolved game, console, global, default") {
    const auto dir = FreshDir();
    const Scope game = Scope::Game("gba", "game-1");
    const Scope console = Scope::Console("gba");
    {
        store::Settings global(dir / "settings.json");
        system::SettingsStore store(&global, dir / "settings-overrides.json");
        CHECK(store.Get(VOLUME, game) == 80);
        CHECK(store.Source("test.volume", game) == Layer::Default);

        store.Set(VOLUME, 60);
        CHECK(store.Get(VOLUME, game) == 60);
        CHECK(global.GetNumber("test.volume") == 60); // the global layer is settings.json
        store.Set(VOLUME, 40, console);
        store.Set(VOLUME, 20, game);
        store.Set(REWIND, false, game);
        store.Set(FILTER, std::string("smooth"), console);

        CHECK(store.Get(VOLUME) == 60);
        CHECK(store.Get(VOLUME, console) == 40);
        CHECK(store.Get(VOLUME, Scope::Console("psx")) == 60);
        CHECK(store.Get(VOLUME, game) == 20);
        CHECK(store.Get(VOLUME, Scope::Game("gba", "game-2")) == 40);
        CHECK(store.Source("test.volume", game) == Layer::Game);
        CHECK_FALSE(store.Get(REWIND, game));
        CHECK(store.Get(REWIND, console));
        CHECK(store.Get(FILTER, game) == "smooth");
    }
    // Everything survives a restart.
    store::Settings global(dir / "settings.json");
    system::SettingsStore store(&global, dir / "settings-overrides.json");
    CHECK(store.Get(VOLUME, game) == 20);
    CHECK(store.IsSet("test.volume", game));
    const auto revision = store.Revision();
    store.Clear("test.volume", game);
    CHECK(store.Revision() > revision);
    CHECK(store.Get(VOLUME, game) == 40);
    store.ClearScope(console);
    CHECK(store.Get(VOLUME, game) == 60);
    CHECK(store.Get(FILTER, game) == "sharp");
    store.Clear("test.volume");
    CHECK(store.Get(VOLUME, game) == 80);
    CHECK(global.GetNumber("settings.version") == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("overrides from a newer version are left alone") {
    const auto dir = FreshDir();
    const auto file = dir / "settings-overrides.json";
    const std::string newer = R"({"version":99,"games":{"g":{"test.volume":5}}})";
    REQUIRE(WriteFileAtomic(file, newer));
    system::SettingsStore store(nullptr, file);
    CHECK(store.Get(VOLUME, Scope::Game("gba", "g")) == 80);
    store.Set(VOLUME, 10, Scope::Game("gba", "g"));
    CHECK(store.Get(VOLUME, Scope::Game("gba", "g")) == 10);
    CHECK(ReadTextFile(file) == newer);
    std::filesystem::remove_all(dir);
}

TEST_CASE("the first start is noticed once") {
    const auto dir = FreshDir();
    store::Settings settings(dir / "settings.json");
    const auto first = system::NoteStart(settings);
    CHECK(first.first_run);
    CHECK_FALSE(first.setup_done);
    CHECK_FALSE(first.upgraded);
    system::MarkSetupDone(settings);

    const auto second = system::NoteStart(settings);
    CHECK_FALSE(second.first_run);
    CHECK(second.setup_done);
    CHECK_FALSE(second.upgraded);
    CHECK(second.previous_version == system::Version());

    settings.SetString("app.last_version", "0.0.1");
    CHECK(system::NoteStart(settings).upgraded);
    std::filesystem::remove_all(dir);
}
