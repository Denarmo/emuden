// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "list" layout: one row of text per game (a small cover, the name, its console, when it
// was last played and for how long), and the selected game's cover large beside the list on
// a screen wide enough. Up and Down move by one, Left and Right by a screenful.

#include <algorithm>
#include <cmath>

#include "den/app/games.h"
#include "den/app/strings.h"
#include "den/gfx/canvas.h"
#include "game_views.h"

namespace den::app {

namespace {

constexpr float REVEAL_MARGIN = 12;
constexpr float EDGE = 10;
constexpr float ROW_GAP = 4;
constexpr float WIDE_COLUMNS = 600; // a list this wide shows the last-played and play-time columns

} // namespace

ListView::Geometry ListView::Measure() const {
    const ui::Theme& theme = Theme();
    Geometry g;
    g.list = rect;
    if (spec_->list.art && rect.w >= rect.h * 1.1f && rect.w >= 600) {
        const float w = std::clamp(rect.w * 0.36f, 240.0f, 520.0f);
        g.art = gfx::Rect{spec_->list.art_left ? rect.x : rect.Right() - w, rect.y + EDGE, w, rect.h - EDGE * 2};
        g.list.w -= w + 28;
        g.list.x = spec_->list.art_left ? rect.x + w + 28 : rect.x;
    }
    g.list.x += EDGE;
    g.list.w = std::max(40.0f, g.list.w - EDGE * 2);
    const float row = std::max(theme.text_body * 1.3f + theme.text_small * 1.3f + 14,
                               (rect.h - EDGE * 2) / static_cast<float>(spec_->list.rows) - ROW_GAP);
    g.rows = layout::Rows{games_.size(), row, ROW_GAP};
    return g;
}

gfx::Rect ListView::Row(const Geometry& g, std::size_t index) const {
    return gfx::Rect{g.list.x, g.list.y + EDGE + g.rows.Start(index) - Scroll(), g.list.w, g.rows.row};
}

float ListView::Extent() const {
    return Measure().rows.Extent() + EDGE * 2;
}

float ListView::Reveal(float from) const {
    return Measure().rows.Reveal(index_, from, rect.h - EDGE * 2, REVEAL_MARGIN);
}

gfx::Rect ListView::SelectedRect() const {
    return Row(Measure(), index_);
}

std::optional<std::size_t> ListView::GameAt(float x, float y) const {
    const Geometry g = Measure();
    if (g.art.w > 0 && g.art.Contains(x, y)) {
        return index_;
    }
    if (x < g.list.x || x > g.list.Right()) {
        return std::nullopt;
    }
    return g.rows.At(y - g.list.y - EDGE + Scroll());
}

bool ListView::Move(layout::Move move) {
    const std::size_t count = games_.size();
    const auto page = static_cast<std::size_t>(std::max(1.0f, std::floor(rect.h / Measure().rows.Pitch()) - 1));
    switch (move) {
    case layout::Move::Up:
        if (index_ == 0) {
            return false;
        }
        SelectFromPad(index_ - 1);
        return true;
    case layout::Move::Down:
        if (index_ + 1 >= count) {
            return false;
        }
        SelectFromPad(index_ + 1);
        return true;
    case layout::Move::Left:
        SelectFromPad(index_ > page ? index_ - page : 0);
        return true;
    case layout::Move::Right:
        SelectFromPad(std::min(count - 1, index_ + page));
        return true;
    }
    return false;
}

void ListView::DrawRow(ui::Context& ctx, const Geometry& g, std::size_t index) {
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Text& text = ctx.Text();
    const ui::Theme& theme = *ctx.theme;
    const library::Entry& game = games_[index];
    const bool selected = index == index_;
    const gfx::Rect r = Row(g, index);
    if (selected) {
        canvas.FillRect(r, theme.panel_strong, theme.radius_small);
    } else if (index % 2 == 1) {
        canvas.FillRect(r, theme.panel.WithAlpha(theme.panel.a / 255.0f * 0.45f), theme.radius_small);
    }
    float x = r.x + 12;
    if (spec_->list.thumbnails) {
        const float h = r.h - 10;
        const float w = std::clamp(h * aspect_, h * 0.6f, h * 1.5f);
        const gfx::Rect thumb{x, r.y + 5, w, h};
        const gfx::Rect drawn = DrawArt(ctx, game, selected ? Grown(thumb) : thumb, selected ? 1.0f : 0.85f, false, 160);
        if (selected && g.art.w <= 0) {
            launch_rect_ = drawn;
        }
        x += w + 14;
    }
    float right = r.Right() - 14;
    const float block = theme.text_body * 1.3f + theme.text_small * 1.2f;
    const float top = r.CenterY() - block * 0.5f;
    if (g.list.w >= WIDE_COLUMNS) {
        const auto column = [&](const std::string& value, float room_w) {
            const std::string shown = text.Elide(value, theme.text_small, room_w);
            const float w = text.Measure(shown, theme.text_small);
            canvas.DrawText(shown, right - w, r.CenterY() - theme.text_small * 0.62f, theme.text_small,
                            selected ? theme.fg : theme.text_dim);
            right -= room_w + 18;
        };
        column(game.play_seconds > 0 ? PlayTimeText(game.play_seconds) : std::string(), 90);
        column(LastPlayedText(game.last_played), 120);
    }
    const float room = std::max(20.0f, right - x);
    canvas.DrawText(text.Elide(game.name, theme.text_body, room, gfx::FONT_BOLD), x, top, theme.text_body,
                    selected ? theme.fg : theme.fg.WithAlpha(0.9f), gfx::FONT_BOLD);
    const std::string under = index < lines_.size() && !lines_[index].empty() ? lines_[index] : SystemName(game.system);
    canvas.DrawText(text.Elide(under, theme.text_small, room), x, top + theme.text_body * 1.3f, theme.text_small,
                    theme.text_dim);
}

void ListView::DrawContent(ui::Context& ctx) {
    if (games_.empty()) {
        return;
    }
    const Geometry g = Measure();
    gfx::Canvas& canvas = *ctx.canvas;
    const ui::Theme& theme = *ctx.theme;
    if (g.art.w > 0) {
        const float details = theme.text_heading * 2.8f + theme.text_body * 1.4f + theme.text_small * 1.4f + 16;
        const gfx::Rect art{g.art.x, g.art.y, g.art.w, std::max(60.0f, g.art.h - details)};
        DrawFeature(ctx, art, true);
        DrawDetails(ctx, gfx::Rect{g.art.x, art.Bottom() + 14, g.art.w, details}, true, 2, theme.text_heading);
    }
    const float top = rect.y + (Scroll() < 1 ? 0 : EDGE);
    canvas.PushClip(gfx::Rect{0, top, canvas.Width(), rect.Bottom() - top});
    const layout::Span span = g.rows.Visible(Scroll() - EDGE, rect.h, 1);
    for (std::size_t i = span.first; i < span.last; ++i) {
        DrawRow(ctx, g, i);
    }
    canvas.PopClip();
    // Where the window is in a long list.
    const float extent = Extent();
    if (extent > rect.h + 1) {
        const float track = rect.h - EDGE * 2;
        const float h = std::max(24.0f, track * rect.h / extent);
        const float y = rect.y + EDGE + (track - h) * std::clamp(Scroll() / std::max(1.0f, scroller.Max()), 0.0f, 1.0f);
        canvas.FillRect(gfx::Rect{g.list.Right() + 3, y, 3, h}, theme.text_dim.WithAlpha(0.5f), 1.5f);
    }
}

} // namespace den::app
