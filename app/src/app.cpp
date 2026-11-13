// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <vector>

#include "automation.h"
#include "title_bar.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/gl.h"
#include "den/gfx/images.h"
#include "den/platform/lifecycle.h"
#include "den/platform/second_display.h"
#include "den/system/device.h"
#include "den/system/first_run.h"
#include "den/system/info.h"
#include "den/system/settings.h"

namespace den::app {

namespace {

// The UI is laid out for a window 720 units high; the scale follows the real height.
constexpr float LOGICAL_HEIGHT = 720.0f;
constexpr float MIN_LOGICAL_WIDTH = 760.0f;
constexpr int KEY_F11 = 0x40000044; // SDLK_F11

// Pixels per logical unit: the window's height over the layout's, times the interface size
// (bigger on small screens), held back where a narrow window would get too few units across.
float PixelScale(int width, int height) {
    const float base = static_cast<float>(height) / LOGICAL_HEIGHT;
    const float widest = std::max(1.0f, static_cast<float>(width) / (MIN_LOGICAL_WIDTH * base));
    return base * std::min(system::UiScale(), widest);
}

// What the button hints start from: a handheld has a pad of its own, a tablet has none.
input::DeviceKind HintDevice() {
    const system::DeviceProfile& device = system::Device();
    if (device.info.builtin_pad || device.form == system::FormFactor::HandheldSmall ||
        device.form == system::FormFactor::HandheldDual) {
        return input::DeviceKind::Handheld;
    }
    return device.form == system::FormFactor::Tablet && device.info.touch ? input::DeviceKind::TouchOnly
                                                                          : input::DeviceKind::Desktop;
}

std::string GlText(gl::Enum name) {
    const unsigned char* text = gl::GetString(name);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

Services g_services;

bool HasArg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

// "--size 1920x1080": the window's size for a windowed start (tests of other resolutions).
void SizeFromArgs(int argc, char** argv, pal::WindowDesc& desc) {
    for (int i = 1; i + 1 < argc; ++i) {
        int width = 0;
        int height = 0;
        if (std::strcmp(argv[i], "--size") == 0 &&
            std::sscanf(argv[i + 1], "%dx%d", &width, &height) == 2 && width >= 320 &&
            height >= 240) {
            desc.width = width;
            desc.height = height;
        }
    }
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
    system::InitSettings(&settings, svc.data_dir);
    system::NoteStart(settings);
    // The device's defaults sit below every setting read from here on.
    system::InitDevice(settings);
    svc.theme = &ui::ThemeByName(settings.GetString("ui.theme", "Light"));

    pal::WindowDesc desc;
    desc.fullscreen = !HasArg(argc, argv, "--windowed") &&
                      (HasArg(argc, argv, "--fullscreen") ||
                       settings.GetBool("window.fullscreen", pal::GetCaps().handheld));
    SizeFromArgs(argc, argv, desc);
    desc.custom_frame = true; // where the platform allows it (Windows), the app draws the title bar
    if (auto icon = gfx::LoadImageFile(pal::AssetsDir() / "branding" / "icon-256.png");
        icon && icon->width == icon->height) {
        desc.icon_size = icon->width;
        desc.icon_rgba = std::move(icon->rgba);
    }
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
            system::NoteGpu(GlText(gl::RENDERER), GlText(gl::VERSION));
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
        input.SetDeviceKind(HintDevice());
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

        TitleBar title_bar;
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
            for (input::Event& event : events) {
                if (title_bar.HandleEvent(event, *svc.window, svc.quit)) {
                    continue;
                }
                using T = input::Event::Type;
                if (event.type == T::PointerDown || event.type == T::PointerMove ||
                    event.type == T::PointerUp || event.type == T::Wheel) {
                    event.y -= static_cast<float>(svc.title_bar_px); // the screens start under the bar
                }
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
            // Android: another app may cover the main window while a game goes on on the
            // second display. The frame runs as always (the game advances in it), drawn for
            // nobody; without a GL context at all there is nothing to run.
            const bool hidden = pal::MainWindowHidden();
            if (!pal::PrepareFrame()) {
                pal::SleepMs(50);
                continue;
            }
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
            // The screens get the window below the app's own title bar, if it has one: with GL's
            // origin at the bottom left, a shorter canvas leaves the bar's rows free.
            // The bar keeps its size as the window is resized: it follows the display's scale.
            const float bar_scale = svc.window->DisplayScale();
            const int bar = title_bar.PixelHeight(*svc.window, bar_scale);
            svc.title_bar_px = bar;
            const float scale = PixelScale(width, height - bar);
            device.BeginFrame(width, height, svc.theme->page);
            canvas.Begin(width, height - bar, scale);
            ui::Context ctx;
            ctx.canvas = &canvas;
            ctx.textures = &textures;
            ctx.theme = svc.theme;
            ctx.family = input.ActiveFamily();
            ctx.show_hints = !input.TouchOnly();
            ctx.dt = dt;
            ctx.time = now;
            stage.Frame(ctx);
            canvas.End();
            canvas.Begin(width, height, bar_scale);
            title_bar.Draw(ctx, *svc.window, width, bar);
            canvas.End();
            automation.AfterDraw(width, height);
            if (!hidden) {
                svc.window->Swap();
            } else if (!pal::GetSecondDisplay().Ready()) {
                pal::SleepMs(100); // nothing of the app is shown anywhere: it idles
            }
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
    system::ShutdownDevice();
    window->reset();
    pal::Shutdown();
    return exit_code;
}

} // namespace den::app
