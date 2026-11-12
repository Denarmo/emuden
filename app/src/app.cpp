// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <vector>

#include "automation.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/system/info.h"

namespace den::app {

namespace {

// The UI is laid out for a window 720 units high; the scale follows the real height.
constexpr float LOGICAL_HEIGHT = 720.0f;
constexpr int KEY_F11 = 0x40000044; // SDLK_F11

Services g_services;

bool HasArg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

Services& Svc() {
    return g_services;
}

void SetTheme(const std::string& name) {
    g_services.theme = &ui::ThemeByName(name);
    g_services.settings->SetString("ui.theme", g_services.theme->name);
    if (ui::Page* page = g_services.stage->Top()) {
        page->Rebuild();
    }
}

void SetFullscreen(bool on) {
    g_services.window->SetFullscreen(on);
    g_services.settings->SetBool("window.fullscreen", on);
}

int Run(int argc, char** argv) {
    if (const Status started = pal::Init(); !started) {
        log::Error("app", "{}", started.error);
        return 1;
    }
    Services& svc = g_services;
    svc.data_dir = pal::DataDir();
    log::SetFile(svc.data_dir / "log.txt");
    log::Info("app", "{} | data in {}", system::Describe(), PathToUtf8(svc.data_dir));

    store::Settings settings(svc.data_dir / "settings.json");
    svc.settings = &settings;
    svc.theme = &ui::ThemeByName(settings.GetString("ui.theme", "Light"));

    pal::WindowDesc desc;
    desc.fullscreen = !HasArg(argc, argv, "--windowed") &&
                      (HasArg(argc, argv, "--fullscreen") ||
                       settings.GetBool("window.fullscreen", pal::GetCaps().handheld));
    auto window = pal::CreateAppWindow(desc);
    if (!window) {
        log::Error("app", "{}", window.error());
        pal::Shutdown();
        return 1;
    }
    svc.window = window->get();

    int exit_code = 0;
    {
        gfx::Device device;
        gfx::Text text;
        gfx::Canvas canvas;
        Status ready = device.Init(pal::GlProcAddress, pal::GetCaps().gles);
        if (ready) {
            ready = text.Init(device);
        }
        if (ready) {
            const auto font = pal::AssetsDir() / "fonts" / "GoogleSans.ttf";
            const auto regular = text.AddFont(font, 400);
            const auto bold = text.AddFont(font, 700);
            if (!regular || !bold) {
                ready = Status::Fail(regular ? bold.error() : regular.error());
            }
        }
        if (ready) {
            ready = canvas.Init(device, text);
        }
        if (!ready) {
            log::Error("app", "{}", ready.error);
            window->reset();
            pal::Shutdown();
            return 1;
        }

        Jobs jobs;
        gfx::TextureCache textures(device, jobs);
        input::System input(svc.window->SdlWindow());
        ui::Stage stage;
        svc.device = &device;
        svc.canvas = &canvas;
        svc.textures = &textures;
        svc.jobs = &jobs;
        svc.input = &input;
        svc.stage = &stage;

        auto library = library::CreateLibrary({svc.data_dir, &jobs, &settings});
        auto plugins = plugins::CreateHost({svc.data_dir, &jobs, &settings});
        auto engine = engine::CreateHost({svc.data_dir, &settings});
        auto player = player::CreateSession({svc.data_dir, &jobs, &device, &settings});
        svc.library = library.get();
        svc.plugins = plugins.get();
        svc.engine = engine.get();
        svc.player = player.get();

        Registry& registry = Registry::Get();
        for (const auto& [route, factory] : registry.Screens()) {
            stage.Register(route, factory);
        }
        stage.on_back_at_root = [] {};
        for (const auto& hook : registry.Startup()) {
            hook();
        }
        stage.Push(stage.HasRoute("setup") && !settings.GetBool("setup.done", false) ? "setup" : "home");

        Automation automation;
        automation.Start();

        std::vector<input::Event> events;
        double last = pal::NowSeconds();
        double fps_time = last;
        int fps_frames = 0;
        while (!svc.quit) {
            const double now = pal::NowSeconds();
            const float dt = static_cast<float>(std::clamp(now - last, 0.0, 0.1));
            last = now;
            if (++fps_frames, now - fps_time >= 0.5) {
                svc.fps = fps_frames / (now - fps_time);
                fps_frames = 0;
                fps_time = now;
            }

            automation.Pump();
            events.clear();
            input.Poll(events);
            for (const input::Event& event : events) {
                if (event.type == input::Event::Type::Quit) {
                    svc.quit = true;
                } else if (event.type == input::Event::Type::Key && event.down && !event.repeat &&
                           event.key == KEY_F11 && pal::GetCaps().windowed) {
                    SetFullscreen(!svc.window->Fullscreen());
                } else {
                    stage.HandleEvent(event);
                }
            }
            jobs.DrainMain();
            textures.Pump();
            for (const auto& hook : registry.Frame()) {
                hook();
            }

            int width = 0, height = 0;
            svc.window->PixelSize(width, height);
            if (width <= 0 || height <= 0) {
                pal::SleepMs(50);
                continue;
            }
            device.BeginFrame(width, height, svc.theme->page);
            canvas.Begin(width, height, static_cast<float>(height) / LOGICAL_HEIGHT);
            ui::Context ctx;
            ctx.canvas = &canvas;
            ctx.textures = &textures;
            ctx.theme = svc.theme;
            ctx.family = input.ActiveFamily();
            ctx.dt = dt;
            ctx.time = now;
            stage.Frame(ctx);
            canvas.End();
            automation.AfterDraw(width, height);
            svc.window->Swap();
            // Without vsync (a hidden or occluded window) the loop must not spin.
            if (pal::NowSeconds() - now < 0.002) {
                pal::SleepMs(8);
            }
        }
        log::Info("app", "closing");
        player->Close();
        if (engine->Running()) {
            engine->Stop();
        }
        // Screens go before the services they use.
        svc.stage = nullptr;
    }
    svc = Services{};
    window->reset();
    pal::Shutdown();
    return exit_code;
}

} // namespace den::app
