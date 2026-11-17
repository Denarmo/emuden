// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <format>

#include "den/app/boot.h"
#include "den/app/second_screen.h"
#include "den/app/game_prefs.h"
#include "den/app/engine_setup.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/library/content.h"
#include "den/platform/lifecycle.h"
#include "den/app/picture_choice.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::Nav;
using input::PadButton;

constexpr int KEY_F3 = 0x4000003C; // SDLK_F3
constexpr int KEY_FAST = 'f';
constexpr int KEY_REWIND = 'r';
constexpr double SELECT_HOLD = 1.0; // Select alone, held: the menu without the chord
constexpr float STICK_HOLD = 0.6f;
constexpr int MAX_PORTS = 4;
constexpr float MENU_CORNER = 72; // logical units: a tap in the top left corner opens the menu
constexpr const char* SIZE_KEY = "player.screen_size";
constexpr const char* FPS_KEY = "player.fps_overlay";
constexpr const char* OLD_FPS_KEY = "player.show_fps"; // on or off, before the graph

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
    AbandonGate();
    StopDisplays();
    if (phase_ == Phase::Running) {
        Svc().player->Close();
    }
    Svc().input->SetNavFromPads(true);
}

void PlayerScreen::Build() {
    if (phase_ == Phase::New) {
        Open();
    }
    // The save check centres its dialog; the menu after it lays out from the edges again.
    Root().align = ui::Align::Stretch;
    Root().justify = ui::Align::Start;
    if (InSaveCheck()) {
        gate_->Build(Root(), focus_after_build_);
        return;
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
    const PlayerStart start{game_, *this};
    std::optional<plugins::CoreInfo> core;
    for (const PlayerHooks& hooks : AllPlayerHooks()) {
        if (hooks.core && (core = hooks.core(start))) {
            break;
        }
    }
    if (!core) {
        core = CoreForGame(game_, Param("core"));
    }
    if (!console || !core || !core->Ready()) {
        return Fail((console ? console->name : game_.name) + ": its core is not installed. Open Consoles to install it.");
    }
    console_ = *console;
    core_ = *core;
    size_ = static_cast<ScreenSize>(std::clamp(ScreenSizeFor(game_), 0,
                                               static_cast<int>(ScreenSizeNames().size()) - 1));
    fps_mode_ = std::clamp(static_cast<int>(svc.settings->GetNumber(
                               FPS_KEY, svc.settings->GetBool(OLD_FPS_KEY, false) ? 1 : 0)),
                           0, FPS_MODES - 1);
    if (!StartGate()) {
        Proceed();
    }
}

void PlayerScreen::Proceed() {
    Services& svc = Svc();
    MatchResumeCore();
    if (InSaveCheck()) {
        phase_ = Phase::Preparing;
        Rebuild();
    }
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

void PlayerScreen::Begin(const std::filesystem::path& content, bool resume) {
    Services& svc = Svc();
    player::OpenRequest request;
    request.core = core_.library;
    request.content = content;
    content_ = content;
    request.system_dir = core_.system_dir;
    request.game_id = game_.id;
    request.system = game_.system;
    request.option_hints = core_.option_hints;
    for (const plugins::ScreenShape& shape : core_.screens) {
        player::ScreenShape& out = request.screens.emplace_back();
        out.frame_width = shape.frame_width;
        out.frame_height = shape.frame_height;
        for (const plugins::ScreenPart& part : shape.screens) {
            out.screens.push_back(player::GameScreen{gfx::Rect{part.x, part.y, part.w, part.h}, 0, 0, part.touch});
        }
    }
    request.resume = resume && Param("resume") != "0";
    request.title = game_.name;
    if (const auto console = svc.plugins->FindConsole(game_.system)) {
        request.allowed = {console->save_states, console->rewind, console->fast_forward, console->cheats};
    }
    const PlayerStart start{game_, *this};
    for (const PlayerHooks& hooks : AllPlayerHooks()) {
        if (hooks.open) {
            hooks.open(start, request);
        }
    }
    if (core_.runner == plugins::Runner::Engine) {
        // An engine app runs it; the session talks to it through the engine host.
        request.engine = EngineInstallFor(core_);
        request.engine_setup = EngineSetupFor(core_);
        request.fullscreen = EngineFullscreenFor(game_, svc.window->Fullscreen());
        request.cover = BootCover();
    }
    log::Info("player", "open {} with {}", game_.name, core_.id);
    if (const Status opened = svc.player->Open(request); !opened) {
        return Fail(opened.error.empty() ? game_.name + " could not be started." : opened.error);
    }
    phase_ = Phase::Running;
    if (BootCovers()) {
        svc.player->SetVolume(0); // the boot screen brings the sound up with the picture
    }
    ReadWidescreen();
    bfi_.Start(game_);
    started_ = pal::NowSeconds();
    started_unix_ = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    aspect_sent_ = 0;
    svc.player->SetAspectOverride(0);
    LoadPicture();
    LoadShortcutMap();
    resume_told_ = false;
    svc.input->SetNavFromPads(false);
    wait_release_ = true;
    pads_seen_ = svc.input->PadCount();
}

void PlayerScreen::Fail(const std::string& message) {
    log::Warn("player", "{}", message);
    StopDisplays();
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
    AbandonGate();
    StopDisplays();
    if (phase_ == Phase::Running) {
        const player::Snapshot snapshot = svc.player->GetSnapshot();
        log::Info("player", "leave {} at frame {} ({}x{})", game_.name, snapshot.frames,
                  snapshot.frame_width, snapshot.frame_height);
        // Close writes the battery save and the state the next start resumes from.
        if (!snapshot.external) {
            svc.library->RecordPlay(game_.id, started_unix_,
                                    static_cast<std::int64_t>(pal::NowSeconds() - started_));
        }
        svc.player->Close();
    }
    for (const PlayerHooks& hooks : AllPlayerHooks()) {
        if (hooks.closed) {
            hooks.closed(game_);
        }
    }
    phase_ = Phase::Left;
    svc.input->SetNavFromPads(true);
    GetStage().Pop();
}

void PlayerScreen::OpenMenu(View view) {
    if (phase_ != Phase::Running || view_ != View::Closed) {
        return;
    }
    Svc().player->Pause();
    log::Info("player", "paused at frame {}", Svc().player->GetSnapshot().frames);
    rewind_toggle_ = false;
    Svc().input->SetNavFromPads(true);
    direct_ = false;
    // The quick settings may have chosen another picture since.
    picture_ = PictureFor(game_);
    picture_scope_ = static_cast<int>(PictureScopeOf(game_));
    Show(view);
}

void PlayerScreen::OpenPage(const std::string& page) {
    if (phase_ != Phase::Running) {
        return;
    }
    const View view = page == "buttons" ? View::Buttons : View::Options;
    if (view == View::Options) {
        const auto options = Svc().player->Options();
        options_for_game_ = std::any_of(options.begin(), options.end(),
                                        [](const player::CoreOption& option) { return option.for_game; });
    }
    if (view_ == View::Closed) {
        OpenMenu(view);
    } else {
        Show(view);
    }
    direct_ = true;
}

void PlayerScreen::SetScreenSize(int index) {
    size_ = static_cast<ScreenSize>(std::clamp(index, 0, static_cast<int>(ScreenSizeNames().size()) - 1));
    Svc().settings->SetNumber(SIZE_KEY, static_cast<int>(size_));
}

void PlayerScreen::CloseMenu() {
    if (view_ == View::Closed) {
        return;
    }
    Svc().player->Resume();
    Svc().input->SetNavFromPads(false);
    wait_release_ = true;
    select_since_ = -1;
    direct_ = false;
    Show(View::Closed);
}

void PlayerScreen::Show(View view, std::string focus) {
    // Another page starts at its top; the same page again stays where it was scrolled to.
    const bool same = view == view_;
    view_ = view;
    focus_after_build_ = std::move(focus);
    PlaceDisplays();
    Rebuild(same);
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
    bool chord = false, guide = false, select_alone = false, hold_fast = false, hold_rewind = false;
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
        select_alone = select_alone || pad.buttons == Bit(PadButton::Select);
        if (std::fabs(pad.rx) > std::fabs(stick)) {
            stick = pad.rx;
        }
        pads[port].buttons &= ~Bit(PadButton::Guide);
    }

    // Select with a shoulder button: rewind (left) and fast-forward (right), on any port.
    for (input::PadState& pad : pads) {
        if (pad.Down(PadButton::Select) && (pad.Down(PadButton::L1) || pad.Down(PadButton::R1))) {
            hold_rewind = hold_rewind || pad.Down(PadButton::L1);
            hold_fast = hold_fast || !pad.Down(PadButton::L1);
            pad.buttons &= ~(Bit(PadButton::Select) | Bit(PadButton::L1) | Bit(PadButton::R1));
        }
    }

    // The menu: the pad's home button, Start and Select together, or Select held alone. Steam
    // keeps the home button of the handhelds it runs on, so there only the other two reach us.
    const double now = pal::NowSeconds();
    const bool guide_pressed = guide && !guide_was_down_;
    guide_was_down_ = guide;
    if (!select_alone) {
        select_since_ = -1;
    } else if (select_since_ < 0) {
        select_since_ = now;
    }
    const bool select_held = select_since_ >= 0 && now - select_since_ >= SELECT_HOLD;
    if (!wait_release_ && (guide_pressed || chord || select_held)) {
        OpenMenu();
        return;
    }

    // Buttons held when the game got the pad back are not for the game, nor any pressed
    // while the boot screen still covers it.
    if (BootCovers()) {
        wait_release_ = true;
    }
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
    // Select with a shoulder button is a switch: each press turns it on or off.
    if (hold_fast && !chord_fast_) {
        fast_toggle_ = !fast_toggle_;
        rewind_toggle_ = false;
    }
    if (hold_rewind && !chord_rewind_) {
        rewind_toggle_ = !rewind_toggle_;
        fast_toggle_ = fast_toggle_ && !rewind_toggle_;
    }
    chord_fast_ = hold_fast;
    chord_rewind_ = hold_rewind;
    FeedShortcuts(pads, MAX_PORTS);
    for (int port = 0; port < MAX_PORTS; ++port) {
        svc.player->SetPad(port, pads[port]);
    }
}

void PlayerScreen::ApplySpeed() {
    const bool playing = view_ == View::Closed;
    // A rewind switched on stops where the history runs out.
    if (rewind_toggle_ && rewind_sent_ && Svc().player->GetSnapshot().rewind_states == 0) {
        rewind_toggle_ = false;
    }
    const bool rewind = playing && (key_rewind_ || stick_rewind_ || rewind_toggle_);
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

void PlayerScreen::SetFpsMode(int mode) {
    fps_mode_ = std::clamp(mode, 0, FPS_MODES - 1);
    Svc().settings->SetNumber(FPS_KEY, fps_mode_);
}

void PlayerScreen::Update(ui::Context& ctx) {
    Services& svc = Svc();
    if (InSaveCheck()) {
        TickGate();
        if (!focus_after_build_.empty()) {
            if (ui::Node* node = Root().FindById(focus_after_build_)) {
                SetFocus(node, false);
                focus_after_build_.clear();
            }
        }
        return;
    }
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
    if (snapshot.state == player::State::Closed || snapshot.game_id != game_.id) {
        // Another page opened a game over this one, or the engine app ended; the session
        // closed this game and kept its saves. Leave quietly, without closing what runs now.
        // The engine host books an engine's play time itself, from its first frame.
        if (!snapshot.external) {
            svc.library->RecordPlay(game_.id, started_unix_,
                                    static_cast<std::int64_t>(pal::NowSeconds() - started_));
        }
        StopDisplays();
        phase_ = Phase::Left;
        svc.input->SetNavFromPads(true);
        GetStage().Pop();
        return;
    }
    if (snapshot.state == player::State::Failed) {
        return Fail(snapshot.error.empty() ? game_.name + " stopped." : snapshot.error);
    }
    // The state is taken on the first frame the core runs, not in Open.
    if (!resume_told_ && snapshot.resumed) {
        resume_told_ = true;
        svc.stage->Toast("Resumed where you left off");
    } else if (!resume_told_ && !snapshot.resume_problem.empty()) {
        resume_told_ = true;
        svc.stage->Toast(snapshot.resume_problem, 5.0f);
    }
    // A pad that went away mid-game (unplugged, battery, out of range) pauses the game.
    const int pads = svc.input->PadCount();
    if (pads < pads_seen_ && view_ == View::Closed) {
        OpenMenu();
        svc.stage->Toast("Controller disconnected");
    }
    pads_seen_ = pads;
    if (view_ == View::Closed) {
        FeedPads();
    }
    ApplySpeed();
    // Black frames only on a game nothing is over, on the display drawn in step with vsync.
    bfi_.Update(snapshot, view_ == View::Closed && !held_ && !BootShowing() && !pal::MainWindowHidden(),
                !swapped_);
    svc.player->Advance(ctx.dt);
    // An engine app paused and handed its menu over.
    if (svc.player->TakeMenuRequest() && view_ == View::Closed) {
        OpenMenu();
    }
    if (fps_mode_ == 2) {
        // The time between the game's frames (not the display's), for the overlay's graph.
        const std::uint64_t frames = svc.player->GetSnapshot().frames;
        if (frames < graph_frames_ || graph_frames_ == 0) {
            graph_frames_ = frames;
            graph_time_ = ctx.time;
        } else if (frames > graph_frames_) {
            const std::uint64_t made = std::min<std::uint64_t>(frames - graph_frames_, frame_ms_.size());
            const float each = static_cast<float>((ctx.time - graph_time_) * 1000.0 / static_cast<double>(made));
            for (std::uint64_t i = 0; i < made; ++i) {
                frame_ms_[frame_at_] = each;
                frame_at_ = (frame_at_ + 1) % frame_ms_.size();
            }
            graph_frames_ = frames;
            graph_time_ = ctx.time;
        }
    } else {
        graph_frames_ = 0;
    }
    dual_.Update();
    PlaceDisplays();
    ApplyOverlay();
}

bool PlayerScreen::OnEvent(const input::Event& event) {
    using Type = input::Event::Type;
    if (event.type == Type::Key) {
        if (event.key == KEY_F3 && event.down && !event.repeat) {
            SetFpsMode((fps_mode_ + 1) % FPS_MODES);
        } else if (event.key == KEY_FAST) {
            key_fast_ = event.down;
        } else if (event.key == KEY_REWIND) {
            key_rewind_ = event.down;
        }
        return true;
    }
    if (InSaveCheck()) {
        return false; // the page's rows have the input; Back leaves (OnNav)
    }
    if (phase_ == Phase::Preparing) {
        if (event.type == Type::Nav && event.nav == Nav::Back && !event.repeat) {
            Leave();
        }
        return true;
    }
    const bool touch = event.type == Type::PointerDown || event.type == Type::PointerMove ||
                       event.type == Type::PointerUp || event.type == Type::Wheel;
    if (touch && phase_ == Phase::Running) {
        const bool menu_here = view_ != View::Closed && !menu_second_;
        if (swapped_ && !menu_here) {
            // The game is on the other display: this window shows the quick settings.
            CompanionStage().HandleEvent(event);
            return true;
        }
        if (view_ != View::Closed && !menu_here) {
            return true; // the menu is on the other display, and the game is paused
        }
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
            if (TabPressed(event, area_, scale)) {
                return true;
            }
            if (event.type == Type::PointerDown && x < MENU_CORNER && y < MENU_CORNER) {
                pointer_down_ = false;
                OpenMenu();
                return true;
            }
            if (dual_.Active()) {
                // Two screens: the touch goes to wherever the touch screen is drawn.
                dual_.Pointer(event, x, y);
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

DEN_REGISTER_SCREEN("player", PlayerScreen);

} // namespace den::app
