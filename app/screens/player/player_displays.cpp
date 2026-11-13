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

#include "../companion/second_screen.h"
#include "player_screen.h"

namespace den::app {

namespace {

constexpr const char* OVERLAY_KEY = "player.overlay_on_second_screen";
constexpr const char* SWAP_KEY = "player.swap_displays";

// The screen whose game runs, and the one that draws on the second display.
PlayerScreen* g_current = nullptr;
PlayerScreen* g_overlay = nullptr;

} // namespace

PlayerScreen* PlayerScreen::Current() {
    return g_current && g_current->phase_ == Phase::Running ? g_current : nullptr;
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

void PlayerScreen::PlaceDisplays() {
    Services& svc = Svc();
    const bool two = phase_ == Phase::Running && SecondScreenReady();
    swapped_ = two && !dual_.Active() && svc.settings->GetBool(SWAP_KEY, false);
    menu_apart_ = two && svc.settings->GetBool(OVERLAY_KEY, true);
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
    if (g_overlay == this) {
        ClearSecondScreenOverlay();
        g_overlay = nullptr;
    }
    if (g_current == this) {
        g_current = nullptr;
    }
    if (second_down_) {
        second_down_ = false;
        Svc().player->SetPointer(0, 0, false);
    }
    swapped_ = menu_apart_ = menu_second_ = false;
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
    }
    if (snapshot.rewinding) {
        badge("\xC2\xAB Rewind", false);
    } else if (snapshot.fast_forward) {
        badge("Fast-forward \xC2\xBB", false);
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
    const gfx::Rect room = choosing ? PlaceMenu(area, true).room : area;
    if (dual_.Active()) {
        // Each screen at its own size; what the second display shows is drawn by its own frame.
        const bool integer = size_ == ScreenSize::PixelPerfect ||
                             svc.settings->GetBool("player.integer_scale", false);
        dual_.DrawMain(ctx, room, integer, view_ == View::Closed);
    } else {
        const Placement placement = Place(size_, room, snapshot.frame_width, snapshot.frame_height,
                                          snapshot.aspect, canvas.Scale());
        if (placement.aspect != aspect_sent_) {
            svc.player->SetAspectOverride(placement.aspect);
            aspect_sent_ = placement.aspect;
        }
        svc.player->Draw(canvas, placement.area);
    }
    DrawOverlays(ctx, area, snapshot);
    if (menu_here) {
        if (!choosing) {
            canvas.FillRect(area, ctx.theme->scrim);
        }
        DrawMenu(ctx, area);
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
        // The menu is touched where it is drawn; a question or a button being asked for
        // on the main display comes first.
        if (menu_second_ && capture_ < 0 && !swallow_nav_ && !GetStage().DialogOpen()) {
            Pointer(event, scale, pal::NowSeconds());
        }
        return;
    }
    if (swapped_) {
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
            const std::string text = "Preparing " + game_.name + "\xE2\x80\xA6";
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
