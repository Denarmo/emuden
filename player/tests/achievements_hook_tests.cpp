// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>

#include <SDL3/SDL_hints.h>
#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/player/session.h"
#include "libretro.h"

using namespace den;
using namespace den::player;
namespace fs = std::filesystem;

namespace {

struct Opened {
    fs::path data;
    std::unique_ptr<Session> session;
};

Opened Open(const char* name, bool hardcore) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    Opened out;
    out.data = fs::temp_directory_path() / "den_player_tests" / name;
    std::error_code ec;
    fs::remove_all(out.data, ec);
    fs::create_directories(out.data, ec);
    const fs::path rom = out.data / "Game.tst";
    std::ofstream(rom, std::ios::binary) << "A";
    Config config;
    config.data_dir = out.data;
    out.session = CreateSession(config);
    OpenRequest request;
    request.core = PathFromUtf8(DEN_TESTCORE_PATH);
    request.content = rom;
    request.game_id = "game-1";
    request.system = "test";
    request.hardcore = hardcore;
    REQUIRE(out.session->Open(request));
    return out;
}

} // namespace

TEST_CASE("the frame hook hears every frame, a reset and a loaded state") {
    Opened game = Open("hook", false);
    Session& session = *game.session;
    int frames = 0, resets = 0, states = 0;
    session.SetFrameHook([&](FrameEvent event) {
        frames += event == FrameEvent::Frame ? 1 : 0;
        resets += event == FrameEvent::Reset ? 1 : 0;
        states += event == FrameEvent::StateLoaded ? 1 : 0;
    });
    for (int i = 0; i < 10; ++i) {
        session.Advance(1.0 / 60.0);
    }
    CHECK(frames == 10);
    session.Reset();
    CHECK(resets == 1);
    REQUIRE(session.SaveState(1));
    REQUIRE(session.LoadState(1));
    CHECK(states == 1);
    // The test core shows its save RAM; it declares no memory map.
    CHECK(session.MemoryRegion(RETRO_MEMORY_SAVE_RAM).size() > 0);
    CHECK(session.MemoryRegion(RETRO_MEMORY_SYSTEM_RAM).empty());
    CHECK(session.MemoryMap() == nullptr);
    CHECK(session.ContentPath().filename() == "Game.tst");
    session.SetFrameHook({});
    session.Close();
}

TEST_CASE("hardcore refuses loading states and rewind") {
    Opened game = Open("hardcore", true);
    Session& session = *game.session;
    Snapshot snapshot = session.GetSnapshot();
    CHECK(snapshot.hardcore);
    CHECK_FALSE(snapshot.can_rewind);
    session.Advance(1.0 / 60.0);
    REQUIRE(session.SaveState(1)); // saving is allowed
    const Status loaded = session.LoadState(1);
    CHECK_FALSE(loaded.ok);
    CHECK(loaded.error.find("Hardcore") != std::string::npos);
    session.SetRewind(true);
    CHECK_FALSE(session.GetSnapshot().rewinding);

    session.SetHardcore(false);
    CHECK(session.GetSnapshot().can_rewind);
    CHECK(session.LoadState(1));
    session.SetHardcore(true);
    CHECK_FALSE(session.GetSnapshot().can_rewind);
    session.Close();
}
