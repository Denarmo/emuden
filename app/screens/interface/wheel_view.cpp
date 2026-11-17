// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "wheel" layout: the games' names on an arc at one side that turns as the selection
// moves, the selected name large in the middle, and the selected game's art filling the rest
// with its records under it. On a narrow screen the names take the whole width over the art.
// Up and Down turn the wheel by one, Left and Right by a wheelful; a finger turns it too.

#include <algorithm>
#include <cmath>

#include "den/app/app.h"
#include "den/gfx/canvas.h"
#include "game_views.h"

namespace den::app {

namespace {

gfx::Color Mix(gfx::Color a, gfx::Color b, float t) {
    const auto mix = [&](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(x + (static_cast<float>(y) - x) * std::clamp(t, 0.0f, 1.0f));
    };
    return gfx::Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

} // namespace

WheelView::Geometry WheelView::Measure() const {
    Geometry g;
    g.half_h = rect.h * 0.5f;
    if (rect.w >= rect.h * 1.05f) {
        const float w = std::clamp(rect.w * 0.42f, 300.0f, 560.0f);
        g.wheel = gfx::Rect{spec_->wheel.left ? rect.x : rect.Right() - w, rect.y, w, rect.h};
        g.art = gfx::Rect{spec_->wheel.left ? rect.x + w : rect.x, rect.y, rect.w - w, rect.h}.Inset(20);
    } else {
        g.wheel = rect;
        g.art = rect;
        g.behind = true;
    }
    return g;
}

float WheelView::Pitch() const {
    return std::max(24.0f, rect.h / static_cast<float>(spec_->wheel.names));
}

float WheelView::NameSize() const {
    return Theme().text_heading * 1.05f;
}

float WheelView::Extent() const {
    return games_.empty() ? 0 : static_cast<float>(games_.size() - 1) * Pitch() + rect.h;
}

WheelView::Name WheelView::NameAt(const Geometry& g, ui::Context* ctx, std::size_t index) const {
    const float offset = static_cast<float>(index) - Place();
    const layout::WheelSpot spot = layout::WheelAt(offset, spec_->wheel.names / 2, spec_->wheel.curve);
    Name name;
    name.size = NameSize() * spot.scale;
    name.alpha = spot.alpha;
    name.weight = std::max(0.0f, 1.0f - std::fabs(offset));
    const float y = rect.CenterY() + spot.y * (g.half_h - Pitch() * 0.5f);
    const float in = std::min(spot.x * g.half_h, g.wheel.w * 0.4f);
    const float h = name.size * 1.8f;
    float x = g.wheel.x + 16, w = g.wheel.w - 32 - in;
    if (g.behind) {
        x += in * 0.5f;
    } else if (!spec_->wheel.left) {
        x += in;
    }
    if (index < games_.size()) {
        // The pill fits the name, at the wheel's inner edge.
        gfx::Text& text = ctx != nullptr ? ctx->Text() : Svc().canvas->GetText();
        const float text_w = text.Measure(games_[index].name, name.size, gfx::FONT_BOLD) + 36;
        if (text_w < w) {
            x = g.behind ? x + (w - text_w) * 0.5f : (spec_->wheel.left ? x + w - text_w : x);
            w = text_w;
        }
    }
    name.rect = gfx::Rect{x, y - h * 0.5f, w, h};
    return name;
}

gfx::Rect WheelView::SelectedRect() const {
    return NameAt(Measure(), nullptr, index_).rect;
}

std::optional<std::size_t> WheelView::GameAt(float x, float y) const {
    const Geometry g = Measure();
    if (!g.behind && g.art.Contains(x, y)) {
        return index_;
    }
    if (!g.wheel.Contains(x, y) || games_.empty()) {
        return std::nullopt;
    }
    std::optional<std::size_t> best;
    float nearest = Pitch() * 0.6f;
    const float place = Place();
    const int half = spec_->wheel.names / 2 + 1;
    for (int k = -half; k <= half; ++k) {
        const float i = std::round(place) + static_cast<float>(k);
        if (i < 0 || i >= static_cast<float>(games_.size())) {
            continue;
        }
        const Name name = NameAt(g, nullptr, static_cast<std::size_t>(i));
        if (name.alpha > 0.2f && std::fabs(name.rect.CenterY() - y) < nearest) {
            nearest = std::fabs(name.rect.CenterY() - y);
            best = static_cast<std::size_t>(i);
        }
    }
    return best;
}

bool WheelView::Move(layout::Move move) {
    const std::size_t count = games_.size();
    const auto page = static_cast<std::size_t>(spec_->wheel.names);
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

void WheelView::DrawContent(ui::Context& ctx) {
    if (games_.empty()) {
        return;
    }
    const Geometry g = Measure();
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Text& text = ctx.Text();
    const ui::Theme& theme = *ctx.theme;

    if (g.behind) {
        DrawFeature(ctx, g.art, false);
        canvas.FillRect(g.art, theme.page.WithAlpha(0.62f), theme.radius);
        launch_rect_ = {};
    } else {
        const float details = theme.text_title * 2.6f + theme.text_body * 1.4f + theme.text_small * 1.4f + 12;
        const gfx::Rect art{g.art.x, g.art.y, g.art.w, std::max(60.0f, g.art.h - details)};
        DrawFeature(ctx, art, true);
        DrawDetails(ctx, gfx::Rect{g.art.x, art.Bottom() + 14, g.art.w, details}, true, 2, theme.text_title);
        // The wheel's side darkens a little, so the names read over any art.
        canvas.FillRect(g.wheel, theme.page.WithAlpha(0.28f), theme.radius);
    }

    canvas.PushClip(rect);
    const float place = Place();
    const int half = spec_->wheel.names / 2 + 2;
    const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor(place) - static_cast<float>(half)));
    const auto last = std::min(games_.size(), static_cast<std::size_t>(std::max(0.0f, std::ceil(place) + half + 1)));
    for (std::size_t i = first; i < last; ++i) {
        const Name name = NameAt(g, &ctx, i);
        if (name.alpha <= 0.01f) {
            continue;
        }
        if (name.weight > 0.01f) {
            canvas.FillRect(name.rect, theme.panel_strong.WithAlpha(theme.panel_strong.a / 255.0f * name.weight),
                            name.rect.h * 0.5f);
        }
        const int font = name.weight > 0.5f ? gfx::FONT_BOLD : gfx::FONT_REGULAR;
        const std::string shown = text.Elide(games_[i].name, name.size, name.rect.w - 36, font);
        const float w = text.Measure(shown, name.size, font);
        float x = name.rect.x + 18;
        if (g.behind) {
            x = name.rect.CenterX() - w * 0.5f;
        } else if (spec_->wheel.left) {
            x = name.rect.Right() - 18 - w;
        }
        const gfx::Color color = Mix(theme.text_dim, theme.fg, name.weight);
        canvas.DrawText(shown, x, name.rect.CenterY() - name.size * 0.62f, name.size, color.WithAlpha(name.alpha),
                        font);
    }
    canvas.PopClip();

    if (g.behind) {
        // The selected game's records along the bottom.
        const std::string line = text.Elide(Line(index_), theme.text_small, rect.w - 40);
        const float w = text.Measure(line, theme.text_small);
        canvas.DrawText(line, rect.CenterX() - w * 0.5f, rect.Bottom() - theme.text_small * 1.5f, theme.text_small,
                        theme.fg);
    }
}

} // namespace den::app
