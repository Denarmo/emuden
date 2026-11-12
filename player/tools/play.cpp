// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Plays one game in a window, without the app: the player's own smoke check.
//   den_player_play --core <core file> --rom <game> [--system gb] [--system-dir <bios folder>]
//       [--data <folder>] [--frames <n>] [--screenshot <png>] [--tap <button>@<frame>]...
//       [--save-state <frame>] [--load-state <frame>] [--no-resume]
// Keyboard: arrows, Z/X/A/S, Q/W, Enter, right Shift; Tab fast-forwards, Backspace rewinds,
// F5 saves slot 0, F8 loads it, Escape leaves. The first gamepad plays too.

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/canvas.h"
#include "den/input/input.h"
#include "den/platform/pal.h"
#include "den/player/session.h"

using namespace den;

namespace {

constexpr int KEY_ESCAPE = 0x1B, KEY_TAB = 0x09, KEY_BACKSPACE = 0x08;
constexpr int KEY_F5 = 0x4000003E, KEY_F8 = 0x40000041;

struct Tap {
    input::PadButton button;
    int frame;
};

struct Args {
    std::filesystem::path core, rom, system_dir, data, screenshot;
    std::string system;
    int frames = 0;
    int save_state_at = -1;
    int load_state_at = -1;
    bool resume = true;
    std::vector<Tap> taps;
};

input::PadButton ButtonByName(const std::string& name) {
    using input::PadButton;
    static const std::pair<const char*, PadButton> NAMES[] = {
        {"south", PadButton::South}, {"east", PadButton::East},   {"west", PadButton::West},
        {"north", PadButton::North}, {"start", PadButton::Start}, {"select", PadButton::Select},
        {"up", PadButton::DpadUp},   {"down", PadButton::DpadDown}, {"left", PadButton::DpadLeft},
        {"right", PadButton::DpadRight}, {"l", PadButton::L1},    {"r", PadButton::R1}};
    for (const auto& [text, button] : NAMES) {
        if (name == text) {
            return button;
        }
    }
    return PadButton::Start;
}

Args Parse(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (flag == "--core") {
            args.core = PathFromUtf8(value());
        } else if (flag == "--rom") {
            args.rom = PathFromUtf8(value());
        } else if (flag == "--system") {
            args.system = value();
        } else if (flag == "--system-dir") {
            args.system_dir = PathFromUtf8(value());
        } else if (flag == "--data") {
            args.data = PathFromUtf8(value());
        } else if (flag == "--screenshot") {
            args.screenshot = PathFromUtf8(value());
        } else if (flag == "--frames") {
            args.frames = std::atoi(value().c_str());
        } else if (flag == "--save-state") {
            args.save_state_at = std::atoi(value().c_str());
        } else if (flag == "--load-state") {
            args.load_state_at = std::atoi(value().c_str());
        } else if (flag == "--no-resume") {
            args.resume = false;
        } else if (flag == "--tap") {
            const std::string tap = value();
            const std::size_t at = tap.find('@');
            if (at != std::string::npos) {
                args.taps.push_back({ButtonByName(tap.substr(0, at)), std::atoi(tap.c_str() + at + 1)});
            }
        }
    }
    return args;
}

int Play(const Args& args) {
    pal::WindowDesc desc;
    desc.title = "EmuDEN player";
    desc.width = 960;
    desc.height = 720;
    auto window = pal::CreateAppWindow(desc);
    if (!window) {
        log::Error("play", "{}", window.error());
        return 1;
    }
    gfx::Device device;
    gfx::Text text;
    gfx::Canvas canvas;
    Status ready = device.Init(pal::GlProcAddress, pal::GetCaps().gles);
    if (ready) {
        ready = text.Init(device);
    }
    if (ready) {
        ready = canvas.Init(device, text);
    }
    if (!ready) {
        log::Error("play", "{}", ready.error);
        return 1;
    }
    (*window)->SetVsync(true);
    input::System input((*window)->SdlWindow());
    input.SetNavFromPads(false);

    player::Config config;
    config.data_dir = args.data.empty() ? pal::DataDir() : args.data;
    config.device = &device;
    auto session = player::CreateSession(config);
    player::OpenRequest request;
    request.core = args.core;
    request.content = args.rom;
    request.system = args.system;
    request.system_dir = args.system_dir.empty() ? config.data_dir / "system" : args.system_dir;
    request.game_id = PathToUtf8(args.rom.stem());
    request.resume = args.resume;
    if (const Status opened = session->Open(request); !opened) {
        return 2;
    }

    std::vector<input::Event> events;
    double last = pal::NowSeconds();
    bool quit = false;
    bool pointer_down = false;
    float pointer_x = 0, pointer_y = 0;
    int exit_code = 0;
    for (int frame = 0; !quit && (args.frames == 0 || frame < args.frames); ++frame) {
        const double now = pal::NowSeconds();
        const double dt = now - last;
        last = now;
        events.clear();
        input.Poll(events);
        for (const input::Event& event : events) {
            using Type = input::Event::Type;
            if (event.type == Type::Quit) {
                quit = true;
            } else if (event.type == Type::Key && !event.repeat) {
                if (event.key == KEY_ESCAPE && event.down) {
                    quit = true;
                } else if (event.key == KEY_TAB) {
                    session->SetFastForward(event.down);
                } else if (event.key == KEY_BACKSPACE) {
                    session->SetRewind(event.down);
                } else if (event.key == KEY_F5 && event.down) {
                    session->SaveState(0);
                } else if (event.key == KEY_F8 && event.down) {
                    session->LoadState(0);
                }
            } else if (event.type == Type::PointerDown || event.type == Type::PointerMove ||
                       event.type == Type::PointerUp) {
                pointer_down = event.type == Type::PointerDown ||
                               (event.type == Type::PointerMove && pointer_down);
                pointer_x = event.x;
                pointer_y = event.y;
            }
        }
        input::PadState pad = input.KeyboardPad();
        if (input.PadCount() > 0) {
            const input::PadState& first = input.Pad(0);
            const std::uint32_t keys = pad.buttons;
            pad = first;
            pad.buttons |= keys;
        }
        for (const Tap& tap : args.taps) {
            if (frame >= tap.frame && frame < tap.frame + 6) {
                pad.buttons |= 1u << static_cast<std::uint32_t>(tap.button);
            }
        }
        session->SetPad(0, pad);
        for (int port = 1; port < 4; ++port) {
            session->SetPad(port, input.Pad(port));
        }

        int width = 0, height = 0;
        (*window)->PixelSize(width, height);
        const float scale = static_cast<float>(height) / 720.0f;
        session->SetPointer(pointer_x / scale, pointer_y / scale, pointer_down);
        session->Advance(dt);
        if (frame == args.save_state_at) {
            const Status saved = session->SaveState(0);
            log::Info("play", "save state: {}", saved ? "ok" : saved.error);
            exit_code = saved ? exit_code : 3;
        }
        if (frame == args.load_state_at) {
            const Status loaded = session->LoadState(0);
            log::Info("play", "load state: {}", loaded ? "ok" : loaded.error);
            exit_code = loaded ? exit_code : 3;
        }

        device.BeginFrame(width, height, gfx::Color::Rgb(0x000000));
        canvas.Begin(width, height, scale);
        session->Draw(canvas, gfx::Rect{0, 0, canvas.Width(), canvas.Height()});
        canvas.End();
        if (!args.screenshot.empty() && args.frames > 0 && frame == args.frames - 1) {
            const std::vector<std::uint8_t> pixels = device.ReadPixels(width, height);
            const Status saved = gfx::SavePng(args.screenshot, width, height, pixels.data());
            log::Info("play", "screenshot: {}", saved ? PathToUtf8(args.screenshot) : saved.error);
        }
        (*window)->Swap();
    }
    const player::Snapshot snapshot = session->GetSnapshot();
    log::Info("play", "{} frames, {:.1f} fps of {:.2f}, {}x{}{}", snapshot.frames,
              snapshot.measured_fps, snapshot.core_fps, snapshot.frame_width, snapshot.frame_height,
              snapshot.hardware_rendered ? ", GPU" : "");
    session->Close();
    return exit_code;
}

} // namespace

int main(int argc, char** argv) {
    const Args args = Parse(argc, argv);
    if (args.core.empty()) {
        log::Error("play", "usage: den_player_play --core <core> --rom <game> [--system <id>]");
        return 1;
    }
    if (const Status started = pal::Init(); !started) {
        log::Error("play", "{}", started.error);
        return 1;
    }
    const int code = Play(args);
    pal::Shutdown();
    return code;
}
