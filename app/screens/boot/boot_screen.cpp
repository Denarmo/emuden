// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The boot screen's course: when the game is started under it, when it is ready to be shown,
// a press that skips ahead, and the fade into the game with its sound coming up.

#include <algorithm>
#include <cmath>
#include <optional>

#include "den/app/player.h"
#include "den/app/boot.h"
#include "boot_draw.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/device.h"
#include "den/player/boot_sound.h"

namespace den::app {

namespace {

constexpr float FADE_IN = 0.4f;    // into the game
constexpr float FADE_BACK = 0.3f;  // back to the page when the game did not start
constexpr float LONGEST_WAIT = 25; // seconds after the start: shown whatever it draws
constexpr int STEADY_FRAMES = 10;  // frames of one size before the picture counts as real
constexpr float ENGINE_TAKES = 1.8f; // an engine app's window has taken the display by then

struct Boot {
    bool active = false;
    BootDraw draw;
    BootRunner runner = BootRunner::Player;
    std::function<void()> start;
    bool started = false, sounded = false;
    float since_start = 0;
    enum class End { None, Reveal, Back } end = End::None;
    float out = 0; // 0..1 through the fade at the end
    double volume = 1;
    std::uint64_t last_frames = 0;
    int last_w = 0, last_h = 0, steady = 0;
    std::uint32_t pad_buttons = ~0u; // held when it began: only new presses skip
    std::optional<gfx::Rect> from;
    double from_time = -10;
    BootWait wait;      // this frame's, from what the game waits for
    BootWait last_wait; // the last frame's, for the input events before this frame's update
    // PlayerApp: the picture handed to the engine app, read after the frame that shows it.
    enum class Cover { None, Wanted, Written, Failed } cover = Cover::None;
};

std::filesystem::path CoverFile() {
    return Svc().data_dir / "cache" / "boot-cover.png";
}

Boot& Get() {
    static Boot boot;
    return boot;
}

void SetGameVolume(float share) {
    Boot& b = Get();
    if (b.started && b.runner == BootRunner::Player) {
        Svc().player->SetVolume(static_cast<float>(b.volume) * std::clamp(share, 0.0f, 1.0f));
    }
}

void Finish() {
    Boot& b = Get();
    SetGameVolume(1);
    b.active = false;
    b.start = nullptr;
    b.draw = BootDraw{};
}

void Skip(const BootWait& wait) {
    Boot& b = Get();
    if (b.end != Boot::End::None) {
        return;
    }
    const BootTimes& times = b.draw.Times();
    if (b.draw.t >= times.logo_end && wait.skip) {
        wait.skip(); // the game starts with what it has
        return;
    }
    if (b.draw.t < times.plain) {
        b.draw.t = times.plain;
        b.sounded = true; // straight to the logo, without its sound
    } else if (b.draw.t < times.logo_end) {
        b.draw.t = times.logo_end;
    }
}

// Whether the game shows real frames, or never will: the boot screen goes either way.
void Watch(float dt) {
    Boot& b = Get();
    Services& svc = Svc();
    b.since_start += dt;
    if (b.runner == BootRunner::Engine) {
        if (!svc.engine->Running() && b.since_start > 0.6f) {
            b.end = Boot::End::Back; // it did not start; the toast says why
        } else if (b.since_start >= ENGINE_TAKES && b.draw.t >= b.draw.Times().logo_end) {
            Finish(); // its window has the display; nothing of this one is seen
        }
        return;
    }
    const ui::Page* top = svc.stage->Top();
    const bool gone = b.since_start > 0.5f && (!top || top->Route() != "player");
    if (gone || svc.stage->DialogOpen()) {
        b.end = Boot::End::Back; // a question to answer first, or it failed
        return;
    }
    const player::Snapshot snap = svc.player->GetSnapshot();
    if (snap.state == player::State::Failed) {
        b.end = Boot::End::Back;
        return;
    }
    bool ready = false;
    if (snap.state == player::State::Running && snap.game_id == b.draw.game.id) {
        if (snap.external) {
            if (b.since_start >= ENGINE_TAKES && b.draw.t >= b.draw.Times().logo_end) {
                Finish();
                return;
            }
        } else if (snap.frames > b.last_frames) {
            const bool same = snap.frame_width == b.last_w && snap.frame_height == b.last_h;
            b.steady = same ? b.steady + static_cast<int>(snap.frames - b.last_frames) : 0;
            b.last_frames = snap.frames;
            b.last_w = snap.frame_width;
            b.last_h = snap.frame_height;
        }
        ready = b.steady >= STEADY_FRAMES && b.last_w > 0;
    }
    if ((ready || b.since_start > LONGEST_WAIT) && b.draw.t >= b.draw.Times().logo_end) {
        log::Info("boot", "{} shown after {:.2f} s ({} frames)", b.draw.game.name, b.since_start, b.last_frames);
        b.end = Boot::End::Reveal;
    }
}

void Tick(ui::Context& ctx) {
    Boot& b = Get();
    const float dt = std::min(ctx.dt, 1.0f / 30); // a long frame (a core loading) pauses it
    if (b.end == Boot::End::None) {
        b.draw.t += dt;
    }
    // A pad's new press skips ahead (the pads give no events once the game has them).
    std::uint32_t buttons = 0;
    for (int port = 0; port < 4; ++port) {
        buttons |= Svc().input->Pad(port).buttons;
    }
    if (buttons & ~b.pad_buttons) {
        Skip(b.wait);
    }
    b.pad_buttons = buttons;

    const BootTimes& times = b.draw.Times();
    if (b.runner == BootRunner::PlayerApp && !b.started && b.draw.t >= times.start) {
        b.draw.t = times.start; // still, while its picture is read
    }
    if (!b.sounded && b.draw.animate && b.draw.t >= times.sound) {
        b.sounded = true;
        if (Svc().settings->GetBool(LAUNCH_SOUND_KEY, true)) {
            player::PlayBootSound(static_cast<float>(b.volume) * 0.8f);
        }
    }
    if (!b.started && b.draw.t >= times.start && b.runner == BootRunner::PlayerApp && b.cover != Boot::Cover::Written &&
        b.cover != Boot::Cover::Failed) {
        b.cover = Boot::Cover::Wanted; // the app starts once the frame is read
    } else if (!b.started && b.draw.t >= times.start) {
        b.started = true;
        if (auto start = std::move(b.start)) {
            start(); // the screens are covered: the game's own start is not seen
        }
    }
    if (b.started && b.end == Boot::End::None) {
        Watch(dt);
    }
    if (!b.active) {
        return;
    }
    if (b.end != Boot::End::None) {
        b.out = std::min(1.0f, b.out + dt / (b.end == Boot::End::Reveal ? FADE_IN : FADE_BACK));
        SetGameVolume(b.end == Boot::End::Reveal ? b.out : 1);
        if (b.out >= 1) {
            Finish();
        }
    }
}

void Overlay(ui::Context& ctx) {
    Boot& b = Get();
    if (!b.active) {
        return;
    }
    Tick(ctx);
    if (b.active) {
        b.draw.Main(ctx, b.out, b.wait.text, b.wait.fraction);
    }
    b.last_wait = std::move(b.wait);
    b.wait = BootWait{}; // set again by the next frame's update while it lasts
}

void AfterDraw(int width, int height) {
    Boot& b = Get();
    if (!b.active || b.cover != Boot::Cover::Wanted) {
        return;
    }
    const std::vector<std::uint8_t> pixels = Svc().device->ReadPixels(width, height);
    const std::filesystem::path file = CoverFile();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const Status saved = gfx::SavePng(file, width, height, pixels.data());
    if (!saved) {
        log::Warn("boot", "no cover for the engine app: {}", saved.error);
    }
    b.cover = saved ? Boot::Cover::Written : Boot::Cover::Failed;
}

bool Filter(const input::Event& event) {
    Boot& b = Get();
    if (!b.active || b.end != Boot::End::None) {
        return false;
    }
    using Type = input::Event::Type;
    switch (event.type) {
    case Type::Nav:
        if (!event.repeat && event.nav != input::Nav::None) {
            Skip(b.last_wait);
        }
        return true;
    case Type::Key:
        if (event.down && !event.repeat) {
            Skip(b.last_wait);
        }
        return true;
    case Type::PointerDown:
        Skip(b.last_wait);
        return true;
    case Type::PointerMove:
    case Type::PointerUp:
    case Type::Wheel:
    case Type::Text:
        return true;
    default:
        return false;
    }
}

} // namespace

bool LaunchAnimationOn() {
    return Svc().settings->GetBool(LAUNCH_ANIMATION_KEY, true);
}

void NoteLaunchFrom(const gfx::Rect& where) {
    Boot& b = Get();
    b.from = where;
    b.from_time = pal::NowSeconds();
}

bool BootGame(const library::Entry& game, BootRunner runner, std::function<void()> start) {
    Boot& b = Get();
    if (b.active) {
        log::Info("boot", "{} not started: {} is starting", game.name, b.draw.game.name);
        return false;
    }
    Services& svc = Svc();
    std::optional<gfx::Rect> from;
    if (b.from && pal::NowSeconds() - b.from_time < 1.0) {
        from = b.from;
    } else if (const ui::Page* top = svc.stage->Top(); top && top->Focus() && top->Focus()->rect.h > 40) {
        from = top->Focus()->rect;
    }
    b.from.reset();
    const float window_h = svc.canvas->Height();
    b = Boot{};
    b.active = true;
    b.runner = runner;
    b.start = std::move(start);
    b.volume = std::clamp(svc.settings->GetNumber("player.volume", 1.0), 0.0, 1.0);
    b.draw.Begin(game, LaunchAnimationOn(), from, window_h, runner == BootRunner::PlayerApp);
    log::Info("boot", "start {} ({})", game.name, b.draw.animate ? "animated" : "plain");
    return true;
}

bool BootCarries(const std::string& game_id) {
    const Boot& b = Get();
    return b.active && b.draw.animate && b.draw.game.id == game_id;
}

bool BootCovers() {
    const Boot& b = Get();
    return b.active && b.end == Boot::End::None;
}

bool BootShowing() {
    return Get().active;
}

void SetBootWait(BootWait wait) {
    Boot& b = Get();
    if (b.active) {
        b.wait = std::move(wait);
    }
}

void BootStepAside() {
    Boot& b = Get();
    if (b.active && b.end == Boot::End::None) {
        log::Info("boot", "{}: a question first", b.draw.game.name);
        b.end = Boot::End::Back;
    }
}

std::filesystem::path BootCover() {
    const Boot& b = Get();
    return b.active && b.cover == Boot::Cover::Written ? CoverFile() : std::filesystem::path();
}

void DrawBootSecond(ui::Context& ctx) {
    const Boot& b = Get();
    if (b.active) {
        b.draw.Second(ctx, b.out);
    }
}

DEN_REGISTER_OVERLAY(Overlay);
DEN_REGISTER_EVENT_FILTER(Filter);
DEN_REGISTER_AFTER_DRAW(AfterDraw);

} // namespace den::app
