// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Drives an engine that speaks the Emulator Control Protocol through the player session, the
// way the player screen does, and reports each step:
//   den_engine_try <engine executable> <game file> [engine arguments...]
// launch, first frame, the engine's menu request (pass it --menu-after 1 if it has one),
// pause, save and load a state, the options, resume, stop.

#include <cstdio>
#include <filesystem>

#include "den/base/files.h"
#include "den/engine/host.h"
#include "den/platform/pal.h"
#include "den/player/session.h"
#include "den/store/settings.h"

using namespace den;

namespace {

int g_failed = 0;

void Step(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%-34s %s%s%s\n", what, ok ? "ok" : "FAILED", detail.empty() ? "" : "  ", detail.c_str());
    g_failed += ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: den_engine_try <engine> <game> [engine arguments...]\n");
        return 2;
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "den-engine-try";
    std::filesystem::create_directories(dir);
    store::Settings settings(dir / "settings.json");
    auto host = engine::CreateHost({dir, &settings});
    player::Config config;
    config.data_dir = dir;
    config.settings = &settings;
    auto session = player::CreateEngineSession(config, *host);

    const auto advance = [&](auto done, double seconds) {
        const double until = pal::NowSeconds() + seconds;
        while (pal::NowSeconds() < until) {
            host->Pump();
            session->Advance(0.016);
            if (done()) {
                return true;
            }
            pal::SleepMs(16);
        }
        return false;
    };

    player::OpenRequest request;
    request.content = PathFromUtf8(argv[2]);
    request.game_id = "try-1";
    request.system = "gba";
    request.title = "Try";
    engine::Install install;
    install.executable = PathFromUtf8(argv[1]);
    install.level = 2;
    install.core_id = "try";
    for (int i = 3; i < argc; ++i) {
        install.args.push_back(argv[i]);
    }
    request.engine = install;

    const double opened = pal::NowSeconds();
    const Status open = session->Open(request);
    Step("open", open.ok, open.error);
    if (!open.ok) {
        return 1;
    }
    engine::Control* control = nullptr;
    const bool playing = advance([&] { return (control = host->Control()) && control->Playing(); }, 15);
    Step("hello, launch, first frame", playing, std::to_string(pal::NowSeconds() - opened).substr(0, 4) + " s");
    if (!playing) {
        return 1;
    }
    std::printf("  level %d, states %d, menu %d, settings %d\n", control->Level(), control->Has("states"),
                control->Has("menu"), control->Has("settings"));
    advance([] { return false; }, 1.2);
    const player::Snapshot running = session->GetSnapshot();
    Step("perf", running.measured_fps > 0, std::to_string(running.measured_fps) + " fps");

    if (control->Has("menu")) {
        const bool menu = advance([&] { return session->TakeMenuRequest(); }, 5);
        Step("menu request", menu, menu ? "" : "none within 5 s (start the engine with --menu-after)");
        if (menu) {
            Step("paused by the menu request", session->GetSnapshot().state == player::State::Paused);
        }
    }
    session->Pause();
    advance([&] { return control->Paused(); }, 1);
    Step("pause", control->Paused());

    const Status saved = session->SaveState(2);
    Step("save state (slot 3)", saved.ok, saved.error);
    Step("thumbnail", !session->StateThumbnail(2).empty(), PathToUtf8(session->StateThumbnail(2)));
    const Status loaded = session->LoadState(2);
    Step("load state (slot 3)", loaded.ok, loaded.error);
    const Status empty = session->LoadState(8);
    Step("load an empty slot fails", !empty.ok, empty.error);

    const auto options = session->Options();
    Step("engine settings", !options.empty(), std::to_string(options.size()) + " settings");
    for (const player::CoreOption& option : options) {
        std::printf("  %s = %s (%zu values)\n", option.key.c_str(), option.value.c_str(), option.values.size());
    }
    session->SetFastForward(true);
    session->SetFastForward(false);
    session->SetVolume(0.8f);

    session->Resume();
    advance([&] { return !control->Paused(); }, 2);
    Step("resume", !control->Paused());
    advance([] { return false; }, 0.5);

    const double stop = pal::NowSeconds();
    session->Close();
    const bool ended = advance([&] { return !host->Running(); }, 10);
    Step("stop (resume state, then exit)", ended, std::to_string(pal::NowSeconds() - stop).substr(0, 4) + " s");
    std::printf("%s\n", g_failed == 0 ? "all steps passed" : "some steps failed");
    return g_failed == 0 ? 0 : 1;
}
