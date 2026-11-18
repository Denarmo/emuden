// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the player screen draws where. One display: the game with the pause menu over it.
// Two displays: an open menu takes the display the quick settings are on and the game stays
// as it is on its own ("player.overlay_on_second_screen"), and a one-screen game can change
// places with the quick settings ("player.swap_displays"). For a game with one screen on
// each display the menu takes the second display, whichever screen that shows.

#include <algorithm>
#include <cmath>
#include <iterator>
#include <format>

#include "den/app/strings.h"
#include "den/app/apps_catalog.h"
#include "den/app/beside.h"
#include "den/app/frame.h"
#include "den/app/second_screen.h"
#include "den/base/log.h"
#include "den/platform/lifecycle.h"
#include "player_screen.h"

namespace den::app {

namespace {

constexpr const char* OVERLAY_KEY = "player.overlay_on_second_screen";
constexpr const char* SWAP_KEY = "player.swap_displays";

// The screen whose game runs, and the one that draws on the second display.
PlayerScreen* g_current = nullptr;
PlayerScreen* g_overlay = nullptr;

// The tab that brings the quick settings back over what covers them, where the system lets
// no handle of ours float over the other app: a small disc on the game's own display.
constexpr float TAB_SIZE = 44;
constexpr float FRAME_GRAPH_W = 240, FRAME_GRAPH_H = 80;
constexpr float FRAME_GRAPH_MS = 50; // the graph's top

gfx::Rect TabRect(const gfx::Rect& area) {
    return gfx::Rect{area.Right() - TAB_SIZE - 8, area.y + area.h * 0.3f, TAB_SIZE, TAB_SIZE};
}

void DrawTab(ui::Context& ctx, const gfx::Rect& area) {
    gfx::Canvas& canvas = *ctx.canvas;
    const gfx::Rect disc = TabRect(area);
    canvas.FillRect(disc, gfx::Color{24, 24, 24, 150}, TAB_SIZE * 0.5f);
    canvas.StrokeRect(disc, gfx::Color{255, 255, 255, 120}, 1.5f, TAB_SIZE * 0.5f);
    for (int line = -1; line <= 1; ++line) {
        canvas.FillRect(gfx::Rect{disc.CenterX() - 9, disc.CenterY() + line * 6.0f - 1, 18, 2},
                        gfx::Color{255, 255, 255, 200}, 1);
    }
}

} // namespace

PlayerScreen* PlayerScreen::Current() {
    return g_current && g_current->phase_ == Phase::Running ? g_current : nullptr;
}

GamePlayer* CurrentPlayer() {
    return PlayerScreen::Current();
}

std::string RunningGame() {
    if (Svc().player == nullptr) {
        return {};
    }
    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    const bool open = snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
    return open && !snapshot.external ? snapshot.game_id : std::string();
}

void PlayerScreen::SwapDisplays() {
    if (dual_.Active()) {
        dual_.Swap();
    } else {
        Svc().settings->SetBool(SWAP_KEY, !Svc().settings->GetBool(SWAP_KEY, false));
    }
    PlaceDisplays();
    Rebuild();
}

bool PlayerScreen::External() const {
    return Svc().player->GetSnapshot().external;
}

void PlayerScreen::Hold(bool hold) {
    if (phase_ != Phase::Running || External()) {
        held_ = false;
        return;
    }
    if (hold) {
        if (!held_ && view_ == View::Closed &&
            Svc().player->GetSnapshot().state == player::State::Running) {
            Svc().player->Pause();
            held_ = true;
            log::Info("player", "held: the game's display is covered");
        }
    } else if (held_) {
        held_ = false;
        if (view_ == View::Closed) {
            Svc().player->Resume();
            wait_release_ = true;
            log::Info("player", "goes on: the game's display is back");
        }
    }
}

bool PlayerScreen::WantsSecond() const {
    return phase_ == Phase::Running && !dual_.Active() && Svc().settings->GetBool(SWAP_KEY, false);
}

bool PlayerScreen::TabPressed(const input::Event& event, const gfx::Rect& area, float scale) {
    if (event.type != input::Event::Type::PointerDown || !Beside().WantsTab() ||
        !TabRect(area).Inset(-10).Contains(event.x / scale, event.y / scale)) {
        return false;
    }
    BesidePullSettings();
    return true;
}

void PlayerScreen::PlaceDisplays() {
    Services& svc = Svc();
    const bool two = phase_ == Phase::Running && SecondScreenReady();
    swapped_ = two && !dual_.Active() && svc.settings->GetBool(SWAP_KEY, false);
    // While another app covers the main window the menu has no display of its own: it opens
    // over the game, wherever that is.
    menu_apart_ = two && !pal::MainWindowHidden() && svc.settings->GetBool(OVERLAY_KEY, true);
    // Apart from a game on the main display, or over a game on the second one.
    menu_second_ = menu_apart_ != swapped_;
}

void PlayerScreen::ApplyOverlay() {
    if (phase_ == Phase::Running) {
        g_current = this;
    }
    const bool want = phase_ == Phase::Running && (swapped_ || (view_ != View::Closed && menu_second_));
    if (want && g_overlay != this) {
        SecondScreenClient client;
        client.draw = [this](ui::Context& ctx, const gfx::Rect& area) { DrawSecond(ctx, area); };
        client.pointer = [this](const input::Event& event) { SecondPointer(event); };
        SetSecondScreenOverlay(std::move(client));
        g_overlay = this;
    } else if (!want && g_overlay == this) {
        ClearSecondScreenOverlay();
        g_overlay = nullptr;
    }
}

void PlayerScreen::StopDisplays() {
    dual_.Stop();
    bfi_.Stop();
    if (g_overlay == this) {
        ClearSecondScreenOverlay();
        g_overlay = nullptr;
    }
    if (g_current == this) {
        g_current = nullptr;
    }
    touch_.Stop();
    held_ = false;
    if (second_down_) {
        second_down_ = false;
        Svc().player->SetPointer(0, 0, false);
    }
    swapped_ = menu_apart_ = menu_second_ = false;
}

// The last frames' times as a line, under guides at 60 and 30 frames a second, with the
// average and the slowest frame above it.
void PlayerScreen::DrawFrameGraph(gfx::Canvas& canvas, const gfx::Rect& box, float text_size) const {
    const gfx::Color ink = gfx::Color::Rgb(0xffffff);
    const gfx::Color guide = gfx::Color{255, 255, 255, 60};
    canvas.FillRect(box, gfx::Color{0, 0, 0, 150}, 8);
    const gfx::Rect plot{box.x + 10, box.y + text_size + 12, box.w - 20, box.h - text_size - 20};
    const auto y_of = [&](float ms) {
        return plot.Bottom() - std::clamp(ms / FRAME_GRAPH_MS, 0.0f, 1.0f) * plot.h;
    };
    for (const float ms : {1000.0f / 60.0f, 1000.0f / 30.0f}) {
        canvas.FillRect({plot.x, y_of(ms), plot.w, 1}, guide);
    }
    const std::size_t count = frame_ms_.size();
    const float step = plot.w / static_cast<float>(count - 1);
    float sum = 0, slowest = 0;
    float last_y = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const float ms = frame_ms_[(frame_at_ + i) % count];
        sum += ms;
        slowest = std::max(slowest, ms);
        const float x = plot.x + step * static_cast<float>(i);
        const float y = y_of(ms);
        if (i > 0) {
            // A step to the new height, then along to the next sample.
            canvas.FillRect({x - 1, std::min(last_y, y) - 1, 2, std::fabs(y - last_y) + 2}, ink);
        }
        canvas.FillRect({x - 1, y - 1, std::min(step, plot.Right() - x) + 2, 2}, ink);
        last_y = y;
    }
    canvas.DrawText(std::format("{:.1f} ms  ·  slowest {:.1f} ms", sum / static_cast<float>(count), slowest),
                    box.x + 10, box.y + 6, text_size, ink, gfx::FONT_BOLD);
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
    if (fps_mode_ > 0) {
        // Frame rate, the sound waiting to be played, and how often it ran dry.
        // Each number sits right-aligned in a slot as wide as its widest value, so the box
        // and the words in it stay where they are while the digits change.
        struct Field {
            std::string number, widest, unit;
        };
        const std::string gaps = std::format("{}", snapshot.audio_underruns);
        const Field fields[] = {
            {std::format("{:.0f}", snapshot.measured_fps), "888", " fps"},
            {std::format("{:.0f}", snapshot.audio_queued_ms), "888", " ms"},
            {gaps, std::string(std::max<std::size_t>(2, gaps.size()), '8'), " gaps"},
        };
        const std::string dot = "  ·  ";
        const auto measure = [&](const std::string& text) { return ctx.Text().Measure(text, size, gfx::FONT_BOLD); };
        float w = 20 + 2 * measure(dot);
        for (const Field& field : fields) {
            w += std::ceil(measure(field.widest)) + measure(field.unit);
        }
        const gfx::Rect box{area.Right() - w - 12, area.y + 12, w, size + 12};
        canvas.FillRect(box, plate, 8);
        float x = box.x + 10;
        for (std::size_t i = 0; i < std::size(fields); ++i) {
            const Field& field = fields[i];
            const float slot = std::ceil(measure(field.widest));
            canvas.DrawText(field.number, x + slot - measure(field.number), box.y + 5, size, ink, gfx::FONT_BOLD);
            canvas.DrawText(field.unit, x + slot, box.y + 5, size, ink, gfx::FONT_BOLD);
            x += slot + measure(field.unit);
            if (i + 1 < std::size(fields)) {
                canvas.DrawText(dot, x, box.y + 5, size, ink, gfx::FONT_BOLD);
                x += measure(dot);
            }
        }
        if (fps_mode_ == 2) {
            DrawFrameGraph(canvas, gfx::Rect{box.Right() - FRAME_GRAPH_W, box.Bottom() + 8, FRAME_GRAPH_W,
                                             FRAME_GRAPH_H}, size);
        }
    }
    if (PadWithOtherApp()) {
        // An app opened on the other display has the keys and the pad; Android gives them
        // back to the window that is touched.
        const std::string text = "The pad is with the app on the other screen. Tap the game to take it back";
        const float w = std::min(area.w - 24, ctx.Text().Measure(text, size, gfx::FONT_BOLD) + 20);
        const gfx::Rect box{area.CenterX() - w * 0.5f, area.Bottom() - size - 24, w, size + 12};
        canvas.FillRect(box, plate, 8);
        canvas.DrawText(ctx.Text().Elide(text, size, w - 20, gfx::FONT_BOLD), box.x + 10, box.y + 5, size, ink,
                        gfx::FONT_BOLD);
    }
    if (snapshot.rewinding) {
        badge("\xC2\xAB Rewind", false);
    } else if (snapshot.fast_forward) {
        badge("Fast-forward \xC2\xBB", false);
    } else {
        for (const PlayerHooks& hooks : AllPlayerHooks()) {
            if (const std::string text = hooks.status ? hooks.status(snapshot) : std::string(); !text.empty()) {
                badge(text, false);
                break;
            }
        }
    }
    for (const PlayerHooks& hooks : AllPlayerHooks()) {
        if (hooks.overlay) {
            hooks.overlay(ctx, area);
        }
    }
}

// The game on the display it has, and the menu over it when it is on the same one.
void PlayerScreen::DrawGameLayer(ui::Context& ctx, const gfx::Rect& area, bool menu_here) {
    Services& svc = Svc();
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(area, gfx::Color::Rgb(0x000000));
    const player::Snapshot snapshot = svc.player->GetSnapshot();
    // The picture pages leave the game as it is, beside the menu: it is what is being chosen.
    const bool choosing = menu_here && (view_ == View::Picture || view_ == View::PictureAdjust);
    const bool portrait_deck = touch_.PortraitActive(area) && !choosing;
    const gfx::Rect room = choosing ? PlaceMenu(area, true).room : (portrait_deck ? touch_.GameRoom() : area);
    // A black frame of black frame insertion: the game's place stays black on this display,
    // and the second display shows its screen as always.
    const bool black = snapshot.bfi_black && !swapped_;
    svc.black_frame = black;
    if (portrait_deck && !black) {
        touch_.DrawShell(ctx);
    }
    if (dual_.Active()) {
        // Each screen at its own size; what the second display shows is drawn by its own frame.
        const bool integer = size_ == ScreenSize::PixelPerfect ||
                             svc.settings->GetBool("player.integer_scale", false);
        dual_.DrawMain(ctx, room, integer, view_ == View::Closed && !touch_.Visible(), black);
    } else if (!black) {
        const Placement placement = Place(size_, room, snapshot.frame_width, snapshot.frame_height,
                                          snapshot.aspect, canvas.Scale());
        if (placement.aspect != aspect_sent_) {
            svc.player->SetAspectOverride(placement.aspect);
            aspect_sent_ = placement.aspect;
        }
        svc.player->Draw(canvas, placement.area);
    }
    const Services::SafePadding safe = SafeMargins();
    const float room_bottom = portrait_deck ? touch_.GameRoom().Bottom() - area.y : area.h;
    const gfx::Rect overlay_room{
        area.x + safe.left,
        area.y + safe.top,
        std::max(100.0f, area.w - safe.left - safe.right),
        std::max(100.0f, room_bottom - safe.top - (portrait_deck ? 0.0f : safe.bottom)),
    };
    DrawOverlays(ctx, overlay_room, snapshot);
    if (touch_.Visible() && !choosing) {
        touch_.DrawControls(ctx, black);
    }
    if (!menu_here && Beside().WantsTab()) {
        DrawTab(ctx, area);
    }
    if (menu_here) {
        if (!choosing) {
            canvas.FillRect(area, ctx.theme->scrim);
        }
        DrawMenu(ctx, area);
    }
}

void PlayerScreen::HandlePlayTouch(const input::Event& event, float menu_corner) {
    using Type = input::Event::Type;
    const bool is_finger =
        event.type == Type::FingerDown || event.type == Type::FingerMove || event.type == Type::FingerUp;
    const float scale = Svc().canvas->Scale();
    const float x = event.x / scale, y = event.y / scale;
    if (!is_finger && TabPressed(event, area_, scale)) {
        return;
    }
    const TouchPad::InputAction action = touch_.HandleEvent(event, scale);
    if (action.open_menu) {
        pointer_down_ = false;
        OpenMenu();
        return;
    }
    if (action.toggle_fast) {
        fast_toggle_ = !fast_toggle_;
        return;
    }
    if (action.swap_screens) {
        dual_.Swap();
        return;
    }
    if (action.consumed) {
        return;
    }
    const bool down = event.type == Type::FingerDown || event.type == Type::PointerDown;
    if (!touch_.Visible() && down && x < menu_corner && y < menu_corner) {
        pointer_down_ = false;
        OpenMenu();
        return;
    }
    if (dual_.Active()) {
        const std::uint64_t fid = is_finger ? event.finger : 0xFFFFFFFFFFFFFFFEULL;
        dual_.PointerFinger(fid, event.type, x, y);
        return;
    }
    if (event.type == Type::PointerDown || event.type == Type::PointerUp ||
        event.type == Type::FingerDown || event.type == Type::FingerUp) {
        pointer_down_ = down;
        Svc().player->SetPointer(x, y, pointer_down_);
    } else if (event.type == Type::PointerMove || event.type == Type::FingerMove) {
        Svc().player->SetPointer(x, y, is_finger ? true : pointer_down_);
    }
}

void PlayerScreen::DrawMenu(ui::Context& ctx, const gfx::Rect& area) {
    const bool preview = !menu_apart_ && (view_ == View::Picture || view_ == View::PictureAdjust);
    const MenuLayout layout = PlaceMenu(area, preview);
    menu_area_ = area;
    if (built_apart_ != menu_apart_ || (!menu_apart_ && (layout.stacked != menu_.stacked ||
                                                         layout.panel.w != menu_.panel.w ||
                                                         layout.room.h != menu_.room.h))) {
        Rebuild(); // the menu was built for another display, or the window changed under it
    }
    ui::Page::Draw(ctx, area);
}

void PlayerScreen::DrawSecond(ui::Context& ctx, const gfx::Rect& area) {
    if (phase_ != Phase::Running) {
        return;
    }
    second_area_ = area;
    const bool menu_here = view_ != View::Closed && menu_second_;
    if (swapped_) {
        DrawGameLayer(ctx, area, menu_here);
    } else if (menu_here) {
        ctx.canvas->FillRect(area, ctx.theme->page);
        DrawMenu(ctx, area);
    }
}

void PlayerScreen::SecondPointer(const input::Event& event) {
    using Type = input::Event::Type;
    if (phase_ != Phase::Running) {
        return;
    }
    const float scale = std::max(0.001f, SecondScreenScale());
    if (view_ != View::Closed) {
        // The menu is touched where it is drawn; a question on the main display comes first.
        if (menu_second_ && !GetStage().DialogOpen()) {
            Pointer(event, scale, pal::NowSeconds());
        }
        return;
    }
    if (swapped_) {
        if (TabPressed(event, second_area_, scale)) {
            return;
        }
        // The game is here: its picture is the touch screen of the consoles that have one.
        if (event.type != Type::PointerMove) {
            second_down_ = event.type == Type::PointerDown;
        }
        Svc().player->SetPointer(event.x / scale, event.y / scale, second_down_);
    }
}

void PlayerScreen::Draw(ui::Context& ctx, const gfx::Rect& area) {
    gfx::Canvas& canvas = *ctx.canvas;
    area_ = area;
    if (phase_ != Phase::Running) {
        canvas.FillRect(area, gfx::Color::Rgb(0x000000));
        if (phase_ == Phase::Preparing) {
            const std::string text = TrF("Preparing {}\xE2\x80\xA6", game_.name);
            const float size = ctx.theme->text_heading;
            canvas.DrawText(text, area.CenterX() - ctx.Text().Measure(text, size) * 0.5f, area.CenterY() - size,
                            size, gfx::Color::Rgb(0xffffff));
        }
        ui::Page::Draw(ctx, area);
        return;
    }
    const bool menu_here = view_ != View::Closed && !menu_second_;
    if (!swapped_) {
        DrawGameLayer(ctx, area, menu_here);
        return;
    }
    // The game is on the second display: this one has the menu, or the quick settings.
    canvas.FillRect(area, ctx.theme->page);
    if (menu_here) {
        DrawMenu(ctx, area);
    } else {
        DrawCompanionStage(ctx);
    }
}

} // namespace den::app
