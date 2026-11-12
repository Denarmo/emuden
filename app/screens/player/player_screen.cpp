// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <format>

#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/library/content.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::Nav;
using input::PadButton;

constexpr int KEY_F3 = 0x4000003C; // SDLK_F3
constexpr int KEY_FAST = 'f';
constexpr int KEY_REWIND = 'r';
constexpr double CHORD_HOLD = 0.5; // Start and Select together, held
constexpr float STICK_HOLD = 0.6f;
constexpr int MAX_PORTS = 4;
constexpr float MENU_CORNER = 72; // logical units: a tap in the top left corner opens the menu
constexpr const char* SIZE_KEY = "player.screen_size";
constexpr const char* FPS_KEY = "player.show_fps";

constexpr std::uint32_t Bit(PadButton button) {
    return 1u << static_cast<std::uint32_t>(button);
}

} // namespace

PlayerScreen::~PlayerScreen() {
    // At shutdown the app closes the session itself and the services are gone.
    if (!Svc().stage) {
        return;
    }
    if (prepare_) {
        prepare_->Cancel();
    }
    if (phase_ == Phase::Running) {
        Svc().player->Close();
    }
    Svc().input->SetNavFromPads(true);
}

void PlayerScreen::Build() {
    if (phase_ == Phase::New) {
        Open();
    }
    if (view_ != View::Closed && phase_ == Phase::Running) {
        BuildMenu();
    }
}

void PlayerScreen::Open() {
    Services& svc = Svc();
    const auto game = svc.library->Find(Param("id"));
    if (!game) {
        return Fail("That game is not in the library any more.");
    }
    game_ = *game;
    const auto console = svc.plugins->FindConsole(game_.system);
    const auto core = svc.plugins->CoreFor(game_.system);
    if (!console || !core || !core->installed || core->library.empty()) {
        return Fail((console ? console->name : game_.name) + ": its core is not installed. Open Consoles to install it.");
    }
    console_ = *console;
    core_ = *core;
    size_ = static_cast<ScreenSize>(std::clamp(static_cast<int>(svc.settings->GetNumber(SIZE_KEY, 0)), 0,
                                               static_cast<int>(ScreenSizeNames().size()) - 1));
    show_fps_ = svc.settings->GetBool(FPS_KEY, false);

    if (game_.archive_member.empty()) {
        return Begin(PathFromUtf8(game_.path));
    }
    // A game inside a zip archive is unpacked first, off the main thread.
    phase_ = Phase::Preparing;
    prepared_ = std::make_shared<std::filesystem::path>();
    prepare_ = svc.jobs->Run("prepare game", [entry = game_, out = prepared_,
                                              dir = svc.data_dir / "cache" / "roms"](Job& job) {
        auto file = library::PrepareRom(entry, dir, [&job] { return job.CancelRequested(); });
        if (!file) {
            return Status::Fail(file.error());
        }
        *out = file.take();
        return Status{};
    });
}

void PlayerScreen::Begin(const std::filesystem::path& content) {
    Services& svc = Svc();
    player::OpenRequest request;
    request.core = core_.library;
    request.content = content;
    request.system_dir = core_.system_dir;
    request.game_id = game_.id;
    request.system = game_.system;
    request.option_hints = core_.option_hints;
    request.resume = Param("resume") != "0";
    log::Info("player", "open {} with {}", game_.name, core_.id);
    if (const Status opened = svc.player->Open(request); !opened) {
        return Fail(opened.error.empty() ? game_.name + " could not be started." : opened.error);
    }
    phase_ = Phase::Running;
    started_ = pal::NowSeconds();
    started_unix_ = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    aspect_sent_ = 0;
    svc.player->SetAspectOverride(0);
    if (svc.player->GetSnapshot().resumed) {
        svc.stage->Toast("Resumed where you left off");
    }
    svc.input->SetNavFromPads(false);
    wait_release_ = true;
}

void PlayerScreen::Fail(const std::string& message) {
    log::Warn("player", "{}", message);
    if (phase_ == Phase::Running) {
        Svc().player->Close();
    }
    phase_ = Phase::Left;
    Svc().input->SetNavFromPads(true);
    GetStage().Toast(message, 6.0f);
    GetStage().Pop();
}

void PlayerScreen::Leave() {
    Services& svc = Svc();
    if (prepare_) {
        prepare_->Cancel();
    }
    if (phase_ == Phase::Running) {
        // Close writes the battery save and the state the next start resumes from.
        svc.library->RecordPlay(game_.id, started_unix_,
                                static_cast<std::int64_t>(pal::NowSeconds() - started_));
        svc.player->Close();
    }
    phase_ = Phase::Left;
    svc.input->SetNavFromPads(true);
    GetStage().Pop();
}

void PlayerScreen::OpenMenu() {
    if (phase_ != Phase::Running || view_ != View::Closed) {
        return;
    }
    Svc().player->Pause();
    Svc().input->SetNavFromPads(true);
    Show(View::Main);
}

void PlayerScreen::CloseMenu() {
    if (view_ == View::Closed) {
        return;
    }
    Svc().player->Resume();
    Svc().input->SetNavFromPads(false);
    wait_release_ = true;
    chord_since_ = -1;
    Show(View::Closed);
}

void PlayerScreen::Show(View view, std::string focus) {
    view_ = view;
    focus_after_build_ = std::move(focus);
    Rebuild();
}

void PlayerScreen::OnHide() {
    // Another page covers the game: it pauses, and the pads navigate again.
    if (phase_ == Phase::Running && view_ == View::Closed) {
        OpenMenu();
    }
}

void PlayerScreen::FeedPads() {
    Services& svc = Svc();
    const input::System& input = *svc.input;
    input::PadState pads[MAX_PORTS];
    pads[0] = input.KeyboardPad();
    std::uint32_t all = pads[0].buttons;
    bool chord = false, guide = false;
    float stick = 0;
    for (int port = 0; port < MAX_PORTS; ++port) {
        const input::PadState& pad = input.Pad(port);
        if (!pad.connected) {
            continue;
        }
        const std::uint32_t keys = port == 0 ? pads[0].buttons : 0;
        pads[port] = pad;
        pads[port].buttons |= keys;
        all |= pad.buttons;
        chord = chord || (pad.Down(PadButton::Start) && pad.Down(PadButton::Select));
        guide = guide || pad.Down(PadButton::Guide);
        if (std::fabs(pad.rx) > std::fabs(stick)) {
            stick = pad.rx;
        }
        pads[port].buttons &= ~Bit(PadButton::Guide);
    }

    // The menu: the pad's home button, or Start and Select held together.
    const double now = pal::NowSeconds();
    const bool guide_pressed = guide && !guide_was_down_;
    guide_was_down_ = guide;
    if (!chord) {
        chord_since_ = -1;
    } else if (chord_since_ < 0) {
        chord_since_ = now;
    }
    if (!wait_release_ && (guide_pressed || (chord && now - chord_since_ >= CHORD_HOLD))) {
        OpenMenu();
        return;
    }

    // Buttons held when the game got the pad back are not for the game.
    if (wait_release_) {
        if (all != 0) {
            for (input::PadState& pad : pads) {
                pad.buttons = 0;
            }
        } else {
            wait_release_ = false;
        }
    }
    // Consoles without a right stick use it to hold fast-forward and rewind.
    stick_fast_ = console_.right_stick_holds && stick > STICK_HOLD;
    stick_rewind_ = console_.right_stick_holds && stick < -STICK_HOLD;
    for (int port = 0; port < MAX_PORTS; ++port) {
        svc.player->SetPad(port, pads[port]);
    }
}

void PlayerScreen::ApplySpeed() {
    const bool playing = view_ == View::Closed;
    const bool rewind = playing && (key_rewind_ || stick_rewind_);
    const bool fast = !rewind && (fast_toggle_ || (playing && (key_fast_ || stick_fast_)));
    if (fast != fast_sent_) {
        Svc().player->SetFastForward(fast);
        fast_sent_ = fast;
    }
    if (rewind != rewind_sent_) {
        Svc().player->SetRewind(rewind);
        rewind_sent_ = rewind;
    }
}

void PlayerScreen::Update(ui::Context& ctx) {
    Services& svc = Svc();
    if (phase_ == Phase::Preparing && prepare_ && prepare_->Finished()) {
        const JobPtr job = std::move(prepare_);
        prepare_ = nullptr;
        if (job->State() == JobState::Done) {
            Begin(*prepared_);
        } else {
            const std::string error = job->Error();
            Fail(error.empty() ? game_.name + " could not be unpacked." : error);
        }
        return;
    }
    if (phase_ != Phase::Running) {
        return;
    }
    if (!focus_after_build_.empty()) {
        if (ui::Node* node = Root().FindById(focus_after_build_)) {
            SetFocus(node, false);
        }
        focus_after_build_.clear();
    }
    const player::Snapshot snapshot = svc.player->GetSnapshot();
    if (snapshot.state == player::State::Failed || snapshot.state == player::State::Closed) {
        return Fail(snapshot.error.empty() ? game_.name + " stopped." : snapshot.error);
    }
    if (view_ == View::Closed) {
        FeedPads();
    }
    ApplySpeed();
    svc.player->Advance(ctx.dt);
}

bool PlayerScreen::OnEvent(const input::Event& event) {
    using Type = input::Event::Type;
    if (event.type == Type::Key) {
        if (event.key == KEY_F3 && event.down && !event.repeat) {
            show_fps_ = !show_fps_;
            Svc().settings->SetBool(FPS_KEY, show_fps_);
        } else if (event.key == KEY_FAST) {
            key_fast_ = event.down;
        } else if (event.key == KEY_REWIND) {
            key_rewind_ = event.down;
        }
        return true;
    }
    if (phase_ == Phase::Preparing) {
        if (event.type == Type::Nav && event.nav == Nav::Back && !event.repeat) {
            Leave();
        }
        return true;
    }
    if (view_ == View::Closed) {
        // The game has the input: only the way into the menu is left.
        const bool menu = event.type == Type::Nav && !event.repeat &&
                          (event.nav == Nav::Back || event.nav == Nav::Menu || event.nav == Nav::Home);
        const bool pointer = event.type == Type::PointerDown || event.type == Type::PointerMove ||
                             event.type == Type::PointerUp;
        if (pointer) {
            // The picture is a touch screen for the consoles that have one; the corner is the
            // way into the menu without a pad or a keyboard.
            const float scale = Svc().canvas->Scale();
            const float x = event.x / scale, y = event.y / scale;
            if (event.type == Type::PointerDown && x < MENU_CORNER && y < MENU_CORNER) {
                pointer_down_ = false;
                OpenMenu();
                return true;
            }
            if (event.type != Type::PointerMove) {
                pointer_down_ = event.type == Type::PointerDown;
            }
            Svc().player->SetPointer(x, y, pointer_down_);
        }
        if (menu) {
            OpenMenu();
        }
        return true;
    }
    if (event.type == Type::Nav && !event.repeat && (event.nav == Nav::Menu || event.nav == Nav::Home)) {
        CloseMenu();
        return true;
    }
    return false;
}

bool PlayerScreen::OnNav(Nav nav) {
    if (nav != Nav::Back) {
        return false;
    }
    switch (view_) {
    case View::Main:
        CloseMenu();
        break;
    case View::Save:
        Show(View::Main, "menu.save");
        break;
    case View::Load:
        Show(View::Main, "menu.load");
        break;
    case View::Options:
        Show(View::Main, "menu.options");
        break;
    case View::Buttons:
        Show(View::Main, "menu.buttons");
        break;
    case View::Closed:
        break;
    }
    return true;
}

void PlayerScreen::DrawOverlays(ui::Context& ctx, const gfx::Rect& area, const player::Snapshot& snapshot) {
    gfx::Canvas& canvas = *ctx.canvas;
    const gfx::Color ink = gfx::Color::Rgb(0xffffff);
    const gfx::Color plate = gfx::Color{0, 0, 0, 150};
    const float size = ctx.theme->text_small;
    const auto badge = [&](const std::string& text, bool right) {
        const float w = ctx.Text().Measure(text, size, gfx::FONT_BOLD) + 20;
        const gfx::Rect box{right ? area.Right() - w - 12 : area.x + 12, area.y + 12, w, size + 12};
        canvas.FillRect(box, plate, 8);
        canvas.DrawText(text, box.x + 10, box.y + 5, size, ink, gfx::FONT_BOLD);
    };
    if (show_fps_) {
        badge(std::format("{:.0f} fps", snapshot.measured_fps), true);
    }
    if (snapshot.rewinding) {
        badge("\xC2\xAB Rewind", false);
    } else if (snapshot.fast_forward) {
        badge("Fast-forward \xC2\xBB", false);
    }
}

void PlayerScreen::Draw(ui::Context& ctx, const gfx::Rect& area) {
    Services& svc = Svc();
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(area, gfx::Color::Rgb(0x000000));
    if (phase_ == Phase::Running) {
        const player::Snapshot snapshot = svc.player->GetSnapshot();
        const Placement placement = Place(size_, area, snapshot.frame_width, snapshot.frame_height,
                                          snapshot.aspect, canvas.Scale());
        if (placement.aspect != aspect_sent_) {
            svc.player->SetAspectOverride(placement.aspect);
            aspect_sent_ = placement.aspect;
        }
        svc.player->Draw(canvas, placement.area);
        DrawOverlays(ctx, area, snapshot);
        if (view_ != View::Closed) {
            canvas.FillRect(area, ctx.theme->scrim);
        }
    } else if (phase_ == Phase::Preparing) {
        const std::string text = "Preparing " + game_.name + "\xE2\x80\xA6";
        const float size = ctx.theme->text_heading;
        canvas.DrawText(text, area.CenterX() - ctx.Text().Measure(text, size) * 0.5f, area.CenterY() - size,
                        size, gfx::Color::Rgb(0xffffff));
    }
    ui::Page::Draw(ctx, area);
}

DEN_REGISTER_SCREEN("player", PlayerScreen);

} // namespace den::app
