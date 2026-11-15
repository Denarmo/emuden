// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The bar at the top of the browser page: back, forward, reload, the page's title and host
// (the address bar of the browser), the download's progress, the browser's menu and close.

#include <algorithm>
#include <format>

#include "den/ui/glyphs.h"
#include "web_bar.h"

namespace den::app::web {

std::vector<BarHit> DrawWebBar(ui::Context& ctx, const gfx::Rect& area, const BarInput& in) {
    const ui::Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(gfx::Rect{area.x, 0, area.w, BAR_HEIGHT}, theme.panel);
    std::vector<BarHit> hits;
    const float y = (BAR_HEIGHT - BAR_BUTTON) * 0.5f;
    const auto button = [&](float x, bool enabled, BarAction action) {
        const gfx::Rect rect{x, y, BAR_BUTTON, BAR_BUTTON};
        canvas.FillRect(rect, theme.panel, theme.radius);
        hits.push_back({rect, enabled, action});
        return rect;
    };
    const auto ink = [&](bool enabled) { return enabled ? theme.fg : theme.text_dim.WithAlpha(0.35f); };

    // Back and forward: chevrons.
    const bool back = in.live && in.can_back, forward = in.live && in.can_forward;
    gfx::Rect r = button(16, back, BarAction::Back);
    ui::DrawLine(canvas, r.CenterX() + 5, r.y + 14, r.CenterX() - 5, r.CenterY(), 3, ink(back));
    ui::DrawLine(canvas, r.CenterX() - 5, r.CenterY(), r.CenterX() + 5, r.Bottom() - 14, 3, ink(back));
    r = button(72, forward, BarAction::Forward);
    ui::DrawLine(canvas, r.CenterX() - 5, r.y + 14, r.CenterX() + 5, r.CenterY(), 3, ink(forward));
    ui::DrawLine(canvas, r.CenterX() + 5, r.CenterY(), r.CenterX() - 5, r.Bottom() - 14, 3, ink(forward));
    // Reload: a ring with an arrow head.
    r = button(128, in.live, BarAction::Reload);
    canvas.StrokeRect(gfx::Rect{r.CenterX() - 10, r.CenterY() - 10, 20, 20}, ink(in.live), 3, 10);
    ui::DrawLine(canvas, r.CenterX() + 10, r.CenterY() - 2, r.CenterX() + 15, r.CenterY() - 9, 3, ink(in.live));
    ui::DrawLine(canvas, r.CenterX() + 10, r.CenterY() - 2, r.CenterX() + 2, r.CenterY() - 5, 3, ink(in.live));
    // Close: a cross.
    r = button(area.Right() - 16 - BAR_BUTTON, true, BarAction::Close);
    ui::DrawLine(canvas, r.CenterX() - 8, r.CenterY() - 8, r.CenterX() + 8, r.CenterY() + 8, 3, theme.fg);
    ui::DrawLine(canvas, r.CenterX() + 8, r.CenterY() - 8, r.CenterX() - 8, r.CenterY() + 8, 3, theme.fg);
    float right = r.x - 16;
    if (in.browser) {
        // The menu: three dots.
        r = button(right - BAR_BUTTON, in.live, BarAction::Menu);
        for (const float dy : {-9.0f, 0.0f, 9.0f}) {
            canvas.FillRect(gfx::Rect{r.CenterX() - 3, r.CenterY() + dy - 3, 6, 6}, ink(in.live), 3);
        }
        right = r.x - 16;
    }

    const float text_x = 192, text_w = std::max(0.0f, right - text_x);
    if (in.browser && in.live) {
        // The address bar: the title and host, a tap opens the text entry.
        const gfx::Rect field{text_x - 8, y, text_w + 8, BAR_BUTTON};
        canvas.FillRect(field, theme.panel, theme.radius_small);
        hits.push_back({field, true, BarAction::Address});
    }
    canvas.DrawText(ctx.Text().Elide(in.title, theme.text_body, text_w, gfx::FONT_BOLD), text_x, 12, theme.text_body,
                    theme.fg, gfx::FONT_BOLD);
    canvas.DrawText(ctx.Text().Elide(in.line, theme.text_small, text_w), text_x, 42, theme.text_small, theme.text_dim);
    if (in.progress >= 0) {
        canvas.FillRect(gfx::Rect{area.x, BAR_HEIGHT - 3, area.w * std::clamp(in.progress, 0.0f, 1.0f), 3}, theme.fg);
    }
    return hits;
}

} // namespace den::app::web
