// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "grid" layout: rows of covers that scroll, as many per row as fit (or `grid.columns`),
// and a panel with the selected game's cover, name and records beside them, or under them on
// a screen taller than it is wide.

#include <algorithm>
#include <cmath>

#include "den/gfx/canvas.h"
#include "game_views.h"

namespace den::app {

namespace {

constexpr float REVEAL_MARGIN = 20;
constexpr float EDGE = 10; // room for the selection border where the grid is not scrolled

} // namespace

GridView::Geometry GridView::Measure() const {
    const ui::Theme& theme = Theme();
    Geometry g;
    g.gap = std::clamp(spec_->spacing * 0.5f, 8.0f, 28.0f);
    gfx::Rect area = rect;
    if (spec_->grid.panel) {
        if (rect.w >= rect.h * 1.15f) {
            const float w = std::clamp(rect.w * 0.3f, 220.0f, 420.0f);
            g.panel = gfx::Rect{rect.Right() - w, rect.y + EDGE, w, rect.h - EDGE * 2};
            area.w -= w + 24;
        } else {
            const float h = std::clamp(rect.h * 0.3f, theme.text_body * 6, 240.0f);
            g.panel = gfx::Rect{rect.x, rect.Bottom() - h, rect.w, h};
            g.panel_below = true;
            area.h -= h + 16;
        }
    }
    area.x += EDGE;
    area.w = std::max(40.0f, area.w - EDGE * 2);
    g.grid = area;
    const float title_h = spec_->grid.titles ? theme.text_small * 1.7f : 0;
    if (spec_->grid.columns > 0) {
        g.columns = spec_->grid.columns;
    } else {
        const float target_h = std::clamp(area.h * spec_->cover_height * 0.42f, 90.0f, 300.0f);
        g.columns = layout::FitColumns(area.w, target_h * aspect_, g.gap, 2);
    }
    g.cell_w = std::max(1.0f, (area.w - g.gap * static_cast<float>(g.columns - 1)) / static_cast<float>(g.columns));
    g.cover_h = g.cell_w / aspect_;
    const std::size_t cols = static_cast<std::size_t>(g.columns);
    g.rows = layout::Rows{(games_.size() + cols - 1) / cols, g.cover_h + title_h, g.gap};
    return g;
}

gfx::Rect GridView::Cell(const Geometry& g, std::size_t index) const {
    const std::size_t cols = static_cast<std::size_t>(g.columns);
    const float x = g.grid.x + static_cast<float>(index % cols) * (g.cell_w + g.gap);
    const float y = g.grid.y + EDGE + g.rows.Start(index / cols) - Scroll();
    return gfx::Rect{x, y, g.cell_w, g.cover_h};
}

float GridView::Extent() const {
    const Geometry g = Measure();
    // The scroll moves the whole node; the grid is shorter when the panel is under it.
    return g.rows.Extent() + EDGE * 2 + (rect.h - g.grid.h);
}

float GridView::Reveal(float from) const {
    const Geometry g = Measure();
    return g.rows.Reveal(index_ / static_cast<std::size_t>(g.columns), from, g.grid.h - EDGE * 2, REVEAL_MARGIN);
}

gfx::Rect GridView::SelectedRect() const {
    const Geometry g = Measure();
    return Cell(g, index_);
}

std::optional<std::size_t> GridView::GameAt(float x, float y) const {
    const Geometry g = Measure();
    if (g.panel.w > 0 && g.panel.Contains(x, y)) {
        return index_; // the panel shows the selected game
    }
    if (!g.grid.Contains(x, y) || games_.empty()) {
        return std::nullopt;
    }
    const float pitch = g.cell_w + g.gap;
    const auto col = static_cast<std::size_t>((x - g.grid.x) / pitch);
    if (x - g.grid.x - static_cast<float>(col) * pitch > g.cell_w || col >= static_cast<std::size_t>(g.columns)) {
        return std::nullopt;
    }
    const std::optional<std::size_t> row = g.rows.At(y - g.grid.y - EDGE + Scroll());
    const std::size_t index = row ? *row * static_cast<std::size_t>(g.columns) + col : games_.size();
    return index < games_.size() ? std::optional(index) : std::nullopt;
}

bool GridView::Move(layout::Move move) {
    const std::optional<std::size_t> to = layout::GridMove(index_, games_.size(), Measure().columns, move);
    if (!to) {
        return move == layout::Move::Left || move == layout::Move::Right; // the ends of the grid
    }
    SelectFromPad(*to);
    return true;
}

void GridView::DrawContent(ui::Context& ctx) {
    if (games_.empty()) {
        return;
    }
    const Geometry g = Measure();
    gfx::Canvas& canvas = *ctx.canvas;
    const ui::Theme& theme = *ctx.theme;
    const float top = g.grid.y - (Scroll() < 1 ? EDGE : 0);
    const float bottom = g.grid.Bottom() + (Scroll() > scroller.Max() - 1 ? EDGE : 0);
    canvas.PushClip(gfx::Rect{0, top, canvas.Width(), bottom - top});
    const layout::Span span = g.rows.Visible(Scroll() - EDGE, g.grid.h, 1);
    const std::size_t cols = static_cast<std::size_t>(g.columns);
    const float dim = 1.0f - spec_->side_dim * 0.4f;
    gfx::Rect chosen{};
    for (std::size_t row = span.first; row < span.last; ++row) {
        for (std::size_t i = row * cols; i < std::min(games_.size(), (row + 1) * cols); ++i) {
            const gfx::Rect cell = Cell(g, i);
            if (i == index_) {
                chosen = cell;
                continue; // drawn last, over its neighbours
            }
            DrawArt(ctx, games_[i], cell, dim, false);
            if (spec_->grid.titles) {
                const std::string name = ctx.Text().Elide(games_[i].name, theme.text_small, cell.w);
                canvas.DrawText(name, cell.x, cell.Bottom() + theme.text_small * 0.35f, theme.text_small, theme.text_dim);
            }
        }
    }
    if (chosen.w > 0) {
        const gfx::Rect r = Grown(chosen);
        canvas.FillRect(gfx::Rect{r.x + 4, r.Bottom() - 4, r.w - 8, 10}, gfx::Color{0, 0, 0, 255}.WithAlpha(0.3f), 5);
        launch_rect_ = DrawArt(ctx, games_[index_], r, 1, false);
        if (spec_->grid.titles) {
            const std::string name = ctx.Text().Elide(games_[index_].name, theme.text_small, chosen.w, gfx::FONT_BOLD);
            canvas.DrawText(name, chosen.x, chosen.Bottom() + theme.text_small * 0.35f, theme.text_small, theme.fg,
                            gfx::FONT_BOLD);
        }
    } else {
        launch_rect_ = {};
    }
    canvas.PopClip();
    if (g.panel.w > 0) {
        DrawPanel(ctx, g);
    }
}

void GridView::DrawPanel(ui::Context& ctx, const Geometry& g) {
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(g.panel, theme.panel, theme.radius);
    const gfx::Rect inner = g.panel.Inset(16);
    const gfx::Rect cell_launch = launch_rect_;
    if (g.panel_below) {
        const float art_w = std::min(inner.w * 0.4f, inner.h * std::max(aspect_, 0.75f));
        DrawFeature(ctx, gfx::Rect{inner.x, inner.y, art_w, inner.h}, true);
        const gfx::Rect text{inner.x + art_w + 16, inner.y + 4, inner.w - art_w - 16, inner.h};
        DrawDetails(ctx, text, false, 2, theme.text_heading);
    } else {
        const float art_h = std::min(inner.h * 0.56f, inner.w / std::max(0.5f, aspect_));
        DrawFeature(ctx, gfx::Rect{inner.x, inner.y, inner.w, art_h}, true);
        const gfx::Rect text{inner.x, inner.y + art_h + 16, inner.w, inner.h - art_h - 16};
        DrawDetails(ctx, text, false, 3, theme.text_heading);
    }
    if (cell_launch.w > 0) {
        launch_rect_ = cell_launch; // a launch rises from the cover in the grid
    }
}

} // namespace den::app
