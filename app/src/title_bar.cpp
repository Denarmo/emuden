// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "title_bar.h"

#include <cmath>
#include <vector>

#include "den/base/files.h"
#include "den/ui/glyphs.h"
#include "den/ui/theme.h"

namespace den::app {

namespace {

constexpr float BUTTON_W = 46; // logical units, as wide as the system's own buttons
constexpr float GLYPH = 10;
constexpr float MARK = 18;
constexpr float NAME_SIZE = 15;
constexpr gfx::Color CLOSE_RED{232, 17, 35, 255};
constexpr gfx::Color CLOSE_RED_PRESSED{241, 112, 122, 255};

bool IsPointer(const input::Event& event) {
    using T = input::Event::Type;
    return event.type == T::PointerDown || event.type == T::PointerMove || event.type == T::PointerUp ||
           event.type == T::Wheel;
}

} // namespace

int TitleBar::PixelHeight(const pal::Window& window, float scale) const {
    if (!pal::GetCaps().custom_frame || window.Fullscreen()) {
        return 0;
    }
    return static_cast<int>(std::lround(HEIGHT * scale));
}

TitleBar::Button TitleBar::At(float x, float y) const {
    if (bar_px_ <= 0 || y < 0 || y >= bar_px_) {
        return NONE;
    }
    const float w = BUTTON_W * scale_;
    for (int i = 0; i < COUNT; ++i) {
        const float left = width_px_ - (COUNT - i) * w;
        if (x >= left && x < left + w) {
            return static_cast<Button>(i);
        }
    }
    return NONE;
}

bool TitleBar::HandleEvent(const input::Event& event, pal::Window& window, bool& quit) {
    if (bar_px_ <= 0 || !IsPointer(event)) {
        return false;
    }
    using T = input::Event::Type;
    const Button at = At(event.x, event.y);
    const bool over = event.y >= 0 && event.y < bar_px_;
    if (event.type == T::PointerMove) {
        hover_ = at;
        return over || pressed_ != NONE;
    }
    if (event.type == T::PointerDown) {
        pressed_ = at;
        return over;
    }
    if (event.type == T::PointerUp) {
        const Button was = pressed_;
        pressed_ = NONE;
        if (was != NONE && was == at) {
            if (was == MINIMIZE) {
                hover_ = NONE; // the pointer is not over the bar when the window comes back
                window.Minimize();
            } else if (was == MAXIMIZE) {
                window.ToggleMaximize();
            } else {
                quit = true;
            }
        }
        return over || was != NONE;
    }
    return over; // the wheel over the bar scrolls nothing
}

void TitleBar::Draw(ui::Context& ctx, pal::Window& window, int width_px, int bar_px) {
    width_px_ = width_px;
    bar_px_ = bar_px;
    scale_ = ctx.canvas->Scale();
    if (bar_px <= 0) {
        if (hit_test_set_) {
            window.SetTitleBar(0, {});
            hit_test_set_ = false;
        }
        hover_ = pressed_ = NONE;
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const ui::Theme& theme = *ctx.theme;
    const float w = canvas.Width();
    const float h = bar_px / scale_;
    canvas.FillRect({0, 0, w, h}, theme.page);
    canvas.FillRect({0, 0, w, h}, theme.panel);

    // The mark ("D.") and the name, as on the rail.
    static const std::string mark = PathToUtf8(pal::AssetsDir() / "branding" / "mark-white.png");
    if (const gfx::Texture texture = ctx.textures ? ctx.textures->Get(mark, 64) : gfx::Texture{}) {
        canvas.DrawImage({12, (h - MARK) * 0.5f, MARK, MARK}, texture, theme.fg);
    }
    const gfx::FontMetrics metrics = ctx.Text().Metrics(NAME_SIZE, gfx::FONT_BOLD);
    const float text_y = (h - metrics.line_height) * 0.5f;
    float x = 12 + MARK + 7;
    x += canvas.DrawText("Emu", x, text_y, NAME_SIZE, theme.fg, gfx::FONT_REGULAR);
    canvas.DrawText("DEN", x, text_y, NAME_SIZE, theme.fg, gfx::FONT_BOLD);

    // Minimise, maximise (or restore) and close.
    const float line = std::max(1.0f, 1.0f / scale_) * 1.1f;
    std::vector<std::array<int, 4>> holes;
    for (int i = 0; i < COUNT; ++i) {
        const auto button = static_cast<Button>(i);
        const gfx::Rect r{w - (COUNT - i) * BUTTON_W, 0, BUTTON_W, h};
        holes.push_back({static_cast<int>(r.x * scale_), 0, static_cast<int>(std::ceil(r.w * scale_)), bar_px});
        gfx::Color ink = theme.fg;
        if (button == CLOSE && (hover_ == CLOSE || pressed_ == CLOSE)) {
            canvas.FillRect(r, pressed_ == CLOSE ? CLOSE_RED_PRESSED : CLOSE_RED);
            ink = gfx::Color{255, 255, 255, 255};
        } else if (pressed_ == button) {
            canvas.FillRect(r, theme.panel_strong);
            canvas.FillRect(r, theme.panel);
        } else if (hover_ == button) {
            canvas.FillRect(r, theme.panel_strong);
        }
        const float cx = r.x + r.w * 0.5f, cy = h * 0.5f, g = GLYPH * 0.5f;
        if (button == MINIMIZE) {
            canvas.FillRect({cx - g, cy, GLYPH, line}, ink);
        } else if (button == MAXIMIZE && !window.Maximized()) {
            canvas.StrokeRect({cx - g, cy - g, GLYPH, GLYPH}, ink, line, 1.5f);
        } else if (button == MAXIMIZE) {
            // Restore: a window with another behind it.
            const float s = GLYPH - 2;
            canvas.StrokeRect({cx - g, cy - g + 2, s, s}, ink, line, 1.5f);
            canvas.FillRect({cx - g + 2, cy - g, s, line}, ink);
            canvas.FillRect({cx - g + 2 + s - line, cy - g, line, s}, ink);
        } else {
            ui::DrawLine(canvas, cx - g, cy - g, cx + g, cy + g, line, ink);
            ui::DrawLine(canvas, cx - g, cy + g, cx + g, cy - g, line, ink);
        }
    }
    window.SetTitleBar(bar_px, std::move(holes));
    hit_test_set_ = true;
}

} // namespace den::app
