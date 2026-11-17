// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "hero" layout: the selected game large across the screen (its cover, or the picture of
// the moment for a Continue card, with its console, name and records beside it, or under it
// on a tall screen) and a thin row of covers along the bottom that keeps the selection in the
// middle. Left and Right move along the row; a swipe on it scrolls.

#include <algorithm>
#include <cmath>

#include "den/app/games.h"
#include "den/gfx/canvas.h"
#include "game_views.h"

namespace den::app {

HeroView::HeroView(std::shared_ptr<const InterfaceSpec> spec) : GameView(std::move(spec)) {
    axis = ui::Axis::Row;
}

HeroView::Geometry HeroView::Measure() const {
    const ui::Theme& theme = Theme();
    Geometry g;
    const float strip_h = std::max(rect.h * spec_->hero.strip, theme.text_small * 6);
    g.strip = gfx::Rect{rect.x, rect.Bottom() - strip_h, rect.w, strip_h};
    g.hero = gfx::Rect{rect.x, rect.y, rect.w, std::max(40.0f, rect.h - strip_h - 14)};
    g.thumb_h = strip_h - 18;
    g.thumb_w = g.thumb_h * aspect_;
    g.gap = std::clamp(spec_->spacing * 0.4f, 6.0f, 20.0f);
    return g;
}

float HeroView::Pitch() const {
    const Geometry g = Measure();
    return std::max(8.0f, g.thumb_w + g.gap);
}

float HeroView::Extent() const {
    return games_.empty() ? 0 : static_cast<float>(games_.size() - 1) * Pitch() + rect.w;
}

gfx::Rect HeroView::Thumb(const Geometry& g, std::size_t index) const {
    const float cx = rect.CenterX() + (static_cast<float>(index) - Place()) * (g.thumb_w + g.gap);
    return gfx::Rect{cx - g.thumb_w * 0.5f, g.strip.y + 9, g.thumb_w, g.thumb_h};
}

gfx::Rect HeroView::SelectedRect() const {
    const Geometry g = Measure();
    return Thumb(g, index_);
}

std::optional<std::size_t> HeroView::GameAt(float x, float y) const {
    const Geometry g = Measure();
    if (g.hero.Contains(x, y)) {
        return index_;
    }
    if (!g.strip.Contains(x, y) || games_.empty()) {
        return std::nullopt;
    }
    const float at = std::round(Place() + (x - rect.CenterX()) / Pitch());
    if (at < 0 || at >= static_cast<float>(games_.size())) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(at);
    return Thumb(g, index).Contains(x, y) ? std::optional(index) : std::nullopt;
}

bool HeroView::Move(layout::Move move) {
    switch (move) {
    case layout::Move::Left:
        if (index_ > 0) {
            SelectFromPad(index_ - 1);
        }
        return true;
    case layout::Move::Right:
        if (index_ + 1 < games_.size()) {
            SelectFromPad(index_ + 1);
        }
        return true;
    default:
        return false; // up to the tabs, down to the other screens
    }
}

void HeroView::DragTo(float x, float y) {
    if (Launching() || games_.empty()) {
        return;
    }
    if (ticks_ - drag_tick_ > 2) {
        if (const std::optional<std::size_t> at = GameAt(x, y)) {
            SelectFromPad(*at);
        }
        drag_x_ = x;
        drag_pos_ = static_cast<float>(index_);
    } else {
        const float target = drag_pos_ - (x - drag_x_) / Pitch();
        const float last = static_cast<float>(games_.size() - 1);
        SelectFromPad(static_cast<std::size_t>(std::lround(std::clamp(target, 0.0f, last))));
    }
    drag_tick_ = ticks_;
}

void HeroView::DrawContent(ui::Context& ctx) {
    if (games_.empty()) {
        return;
    }
    const Geometry g = Measure();
    gfx::Canvas& canvas = *ctx.canvas;
    const ui::Theme& theme = *ctx.theme;
    const library::Entry& game = games_[index_];
    const bool wide = g.hero.w >= g.hero.h * 1.25f;
    const float name_size = theme.text_title * (wide ? 1.25f : 1.0f);
    const float details = name_size * 1.3f * 3 + theme.text_body * 1.4f + theme.text_small * 2.8f;

    gfx::Rect art, text;
    if (wide) {
        const float w = std::min(g.hero.w * 0.42f, (g.hero.h - 20) * std::max(aspect_, 0.7f));
        art = gfx::Rect{g.hero.x + 10, g.hero.y + 10, w, g.hero.h - 20};
        text = gfx::Rect{art.Right() + 36, g.hero.y + std::max(10.0f, (g.hero.h - details) * 0.4f),
                         g.hero.Right() - art.Right() - 46, details};
    } else {
        const float h = std::max(60.0f, g.hero.h - details - 10);
        art = gfx::Rect{g.hero.x + 10, g.hero.y + 6, g.hero.w - 20, h};
        text = gfx::Rect{g.hero.x + 10, art.Bottom() + 12, g.hero.w - 20, details};
    }
    DrawFeature(ctx, art, true);
    // The console above the name, in the interface's accent.
    const std::string console = SystemName(game.system);
    const float cw = ctx.Text().Measure(console, theme.text_small, gfx::FONT_BOLD);
    canvas.DrawText(console, wide ? text.x : text.CenterX() - cw * 0.5f, text.y, theme.text_small, spec_->accent,
                    gfx::FONT_BOLD);
    text.y += theme.text_small * 1.6f;
    DrawDetails(ctx, text, !wide, 3, name_size);

    canvas.FillRect(g.strip, theme.page.WithAlpha(0.35f), theme.radius);
    canvas.PushClip(g.strip);
    const float place = Place();
    const float reach = rect.w * 0.5f / Pitch() + 2;
    const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor(place - reach)));
    const auto last = std::min(games_.size(), static_cast<std::size_t>(std::max(0.0f, std::ceil(place + reach))));
    const float dim = 1.0f - spec_->side_dim;
    for (std::size_t i = first; i < last; ++i) {
        if (i != index_) {
            DrawArt(ctx, games_[i], Thumb(g, i), dim, false, 192);
        }
    }
    canvas.PopClip();
    DrawArt(ctx, game, Thumb(g, index_), 1, false, 192);
}

} // namespace den::app
