// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <random>

#include <SDL3/SDL_hints.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio_out.h"
#include "core/core.h"
#include "den/base/files.h"
#include "den/player/session.h"
#include "game_files.h"
#include "pad_map.h"
#include "rewind_buffer.h"

using namespace den;
using namespace den::player;
namespace fs = std::filesystem;

namespace {

fs::path TestCore() {
    return PathFromUtf8(DEN_TESTCORE_PATH);
}

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "den_player_tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

fs::path WriteRom(const fs::path& dir, const char* name, const std::string& bytes) {
    const fs::path file = dir / name;
    std::ofstream(file, std::ios::binary) << bytes;
    return file;
}

void QuietAudio() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
}

struct Rgb {
    int r, g, b;
};

Rgb PixelAt(const VideoFrame& frame, unsigned x, unsigned y) {
    const std::uint8_t* p = frame.rgba.data() + (static_cast<std::size_t>(y) * frame.width + x) * 4;
    return {p[0], p[1], p[2]};
}

input::PadState Pad(std::initializer_list<input::PadButton> buttons,
                    input::PadFamily family = input::PadFamily::PlayStation) {
    input::PadState pad;
    pad.connected = true;
    pad.family = family;
    for (const input::PadButton button : buttons) {
        pad.buttons |= 1u << static_cast<std::uint32_t>(button);
    }
    return pad;
}

} // namespace

TEST_CASE("rewind buffer steps back through pushed states") {
    RewindBuffer rewind(1 << 20);
    std::vector<std::vector<std::uint8_t>> states;
    std::mt19937 random(7);
    std::vector<std::uint8_t> state(4096, 0);
    for (int i = 0; i < 20; ++i) {
        for (int k = 0; k < 40; ++k) {
            state[random() % state.size()] = static_cast<std::uint8_t>(random());
        }
        states.push_back(state);
        rewind.Push(state);
    }
    REQUIRE(rewind.Count() == 19);
    std::vector<std::uint8_t> out(state.size());
    for (int i = 18; i >= 0; --i) {
        REQUIRE(rewind.StepBack(out));
        REQUIRE(out == states[static_cast<std::size_t>(i)]);
    }
    REQUIRE_FALSE(rewind.StepBack(out));
}

TEST_CASE("rewind buffer keeps to its budget") {
    RewindBuffer rewind(3000);
    std::vector<std::uint8_t> state(1000);
    for (int i = 0; i < 50; ++i) {
        std::fill(state.begin(), state.end(), static_cast<std::uint8_t>(i));
        rewind.Push(state);
    }
    REQUIRE(rewind.Bytes() <= 3000);
    REQUIRE(rewind.Count() >= 1);
}

TEST_CASE("save states are tied to the core that made them") {
    const std::vector<std::uint8_t> state = {1, 2, 3, 4};
    const std::vector<std::uint8_t> file = WrapState("mGBA", state);
    const auto same = UnwrapState("mGBA", file);
    REQUIRE(same.ok());
    REQUIRE(std::vector<std::uint8_t>(same.value().begin(), same.value().end()) == state);
    REQUIRE_FALSE(UnwrapState("PPSSPP", file).ok());
    REQUIRE_FALSE(UnwrapState("mGBA", state).ok());
}

TEST_CASE("file names are made safe and game paths are per system") {
    REQUIRE(SafeFileName("a/b:c?.gb") == "a_b_c_.gb");
    REQUIRE(SafeFileName("...") == "game");
    const GamePaths paths = MakeGamePaths("data", "gba", "id:1", "roms/Game.gba");
    REQUIRE(paths.BatterySave() == fs::path("data") / "saves" / "gba" / "id_1.srm");
    REQUIRE(paths.State(3) == fs::path("data") / "states" / "gba" / "id_1.slot3.state");
    REQUIRE(MakeGamePaths("data", "", "", "roms/Game.gba").base == "Game");
}

TEST_CASE("content that is not a zip is used as it is") {
    const auto path = ResolveContent("roms/Game.gba", {"gba"}, "cache");
    REQUIRE(path.ok());
    REQUIRE(path.value() == fs::path("roms/Game.gba"));
    REQUIRE_FALSE(ResolveContent("roms/missing.zip", {"gba"}, FreshDir("zip")).ok());
}

TEST_CASE("pads map by position, and by label on Nintendo consoles") {
    using input::PadButton;
    using input::PadFamily;
    REQUIRE(MapPad(Pad({PadButton::South}, PadFamily::Xbox), "psx", true).buttons == RETRO_B);
    REQUIRE(MapPad(Pad({PadButton::South}, PadFamily::Xbox), "gba", true).buttons == RETRO_A);
    REQUIRE(MapPad(Pad({PadButton::East}, PadFamily::Xbox), "gba", true).buttons == RETRO_B);
    REQUIRE(MapPad(Pad({PadButton::South}, PadFamily::Xbox), "gba", false).buttons == RETRO_B);
    REQUIRE(MapPad(Pad({PadButton::East}, PadFamily::Nintendo), "gba", true).buttons == RETRO_A);
    REQUIRE(MapPad(Pad({PadButton::Start, PadButton::DpadLeft}), "gb", true).buttons ==
            (RETRO_START | RETRO_LEFT));
    input::PadState stick = Pad({});
    stick.lx = 0.9f;
    stick.rt = 1.0f;
    REQUIRE(MapPad(stick, "gb", true).buttons == (RETRO_RIGHT | RETRO_R2));
    REQUIRE(MapPad(stick, "psx", true).buttons == RETRO_R2);
    REQUIRE(MapPad(stick, "psx", true).lx > 29000);
}

TEST_CASE("the sound's rate follows its queue") {
    REQUIRE(DriftRatio(0.064, 0.064) == Catch::Approx(1.0));
    REQUIRE(DriftRatio(0.2, 0.064) > 1.0);
    REQUIRE(DriftRatio(0.0, 0.064) < 1.0);
    REQUIRE(DriftRatio(5.0, 0.064) <= 1.0151);
}

TEST_CASE("the core host runs the test core") {
    LibretroCore::LoadParams params;
    params.core = TestCore();
    params.options["testcore_color"] = "green";
    std::string error;
    auto core = LibretroCore::Load(params, &error);
    REQUIRE(core);
    REQUIRE(core->Info().name == "DenTestCore");
    REQUIRE(core->Info().extensions == std::vector<std::string>{"tst", "bin"});
    // One core at a time.
    REQUIRE_FALSE(LibretroCore::Load(params, &error));

    REQUIRE(core->LoadGame({}, &error));
    std::size_t samples = 0;
    core->SetAudioSink([&](std::span<const std::int16_t> batch) { samples += batch.size(); });
    core->RunFrame();
    const VideoFrame& frame = core->Frame();
    REQUIRE(frame.width == 64);
    REQUIRE(frame.height == 48);
    REQUIRE(frame.rgba.size() == 64u * 48u * 4u);
    REQUIRE(samples == 735u * 2u);
    Rgb square = PixelAt(frame, 10, 10);
    REQUIRE(square.g == 255);
    REQUIRE(square.r == 0);

    SECTION("options reach the running core") {
        const auto options = core->Options();
        REQUIRE(options.size() == 1);
        REQUIRE(options[0].label == "Square colour");
        REQUIRE(options[0].values.size() == 3);
        REQUIRE(options[0].value == "green");
        core->SetOption("testcore_color", "blue");
        core->RunFrame();
        REQUIRE(PixelAt(core->Frame(), 10, 10).b == 255);
        // A value the core never offered falls back to its default.
        core->SetOption("testcore_color", "purple");
        REQUIRE(core->GetOption("testcore_color") == "red");
    }
    SECTION("input moves the square and a state brings it back") {
        std::vector<std::uint8_t> state(core->StateSize());
        REQUIRE(core->SaveState(state));
        core->SetButtons(0, RETRO_RIGHT | RETRO_DOWN);
        for (int i = 0; i < 20; ++i) {
            core->RunFrame();
        }
        REQUIRE(PixelAt(core->Frame(), 10, 10).g != 255);
        REQUIRE(PixelAt(core->Frame(), 30, 30).g == 255);
        REQUIRE(core->LoadState(state));
        core->SetButtons(0, 0);
        core->RunFrame();
        REQUIRE(PixelAt(core->Frame(), 10, 10).g == 255);
    }
    SECTION("save RAM counts presses of A") {
        REQUIRE(core->SaveRam().size() == 32);
        core->SetButtons(0, RETRO_A);
        core->RunFrame();
        core->SetButtons(0, 0);
        core->RunFrame();
        REQUIRE(core->SaveRam()[0] == 1);
    }
}

TEST_CASE("a session plays, saves and resumes") {
    QuietAudio();
    const fs::path data = FreshDir("session");
    const fs::path rom = WriteRom(data, "Game.tst", "A");
    Config config;
    config.data_dir = data;
    auto session = CreateSession(config);
    OpenRequest request;
    request.core = TestCore();
    request.content = rom;
    request.game_id = "game-1";
    request.system = "test";

    REQUIRE(session->Open(request));
    Snapshot snapshot = session->GetSnapshot();
    REQUIRE(snapshot.state == State::Running);
    REQUIRE(snapshot.core_name == "DenTestCore");
    REQUIRE(snapshot.frame_width == 64);
    REQUIRE(snapshot.can_save_states);
    REQUIRE(snapshot.can_rewind);
    REQUIRE_FALSE(snapshot.hardware_rendered);
    REQUIRE(session->Options().size() == 1);

    for (int i = 0; i < 60; ++i) {
        session->Advance(1.0 / 60.0);
    }
    REQUIRE(session->GetSnapshot().frames == 60);
    // A 120 Hz display: half a core frame per display frame.
    for (int i = 0; i < 120; ++i) {
        session->Advance(1.0 / 120.0);
    }
    const std::uint64_t frames = session->GetSnapshot().frames;
    REQUIRE(frames >= 110);
    REQUIRE(frames <= 122);

    session->Pause();
    session->Advance(1.0);
    REQUIRE(session->GetSnapshot().frames == frames);
    REQUIRE(session->GetSnapshot().state == State::Paused);
    session->Resume();

    session->SetFastForward(true);
    session->Advance(1.0 / 60.0);
    REQUIRE(session->GetSnapshot().frames > frames + 1);
    session->SetFastForward(false);

    REQUIRE_FALSE(session->HasState(2));
    REQUIRE(session->SaveState(2));
    REQUIRE(session->HasState(2));
    REQUIRE(session->LoadState(2));
    REQUIRE_FALSE(session->LoadState(3));
    REQUIRE_FALSE(session->SaveState(STATE_SLOTS));
    REQUIRE(IsRegularFile(session->StateThumbnail(2)));

    session->SetRewind(true);
    REQUIRE(session->GetSnapshot().rewinding);
    session->Advance(1.0 / 60.0);
    session->SetRewind(false);

    // A (east on a pad mapped by position) is counted in the battery save.
    session->SetPad(0, Pad({input::PadButton::East}));
    session->Advance(1.0 / 60.0);
    session->SetPad(0, Pad({}));
    session->Advance(1.0 / 60.0);
    session->Close();
    REQUIRE(session->GetSnapshot().state == State::Closed);
    const auto save = ReadBinaryFile(data / "saves" / "test" / "game-1.srm");
    REQUIRE(save);
    REQUIRE(save->size() == 32);
    REQUIRE((*save)[0] == 1);
    REQUIRE(IsRegularFile(data / "states" / "test" / "game-1.resume.state"));

    // The next session continues where this one stopped, with its save.
    REQUIRE(session->Open(request));
    session->Advance(1.0 / 60.0);
    REQUIRE(session->GetSnapshot().resumed);
    session->SetPad(0, Pad({input::PadButton::East}));
    session->Advance(1.0 / 60.0);
    session->Close();
    REQUIRE((*ReadBinaryFile(data / "saves" / "test" / "game-1.srm"))[0] == 2);

    request.resume = false;
    REQUIRE(session->Open(request));
    session->Advance(1.0 / 60.0);
    REQUIRE_FALSE(session->GetSnapshot().resumed);
    session->Close();
}

TEST_CASE("a session reports what went wrong") {
    QuietAudio();
    const fs::path data = FreshDir("failures");
    Config config;
    config.data_dir = data;
    auto session = CreateSession(config);
    OpenRequest request;
    request.core = data / "no_such_core.dll";
    request.content = WriteRom(data, "Bad.tst", "X");
    REQUIRE_FALSE(session->Open(request));
    REQUIRE(session->GetSnapshot().state == State::Failed);
    REQUIRE_FALSE(session->GetSnapshot().error.empty());

    request.core = TestCore();
    const Status refused = session->Open(request);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error == session->GetSnapshot().error);
    // The core is free again for the next game.
    request.content = WriteRom(data, "Good.tst", "B");
    REQUIRE(session->Open(request));
    session->Close();
}
