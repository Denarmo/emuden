// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the quick settings layouts share: the actions' icons, drawn with the canvas; a tile
// for an action; the cover with the game's name and status.

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "companion_quick.h"
#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/second_screen.h"
#include "den/gfx/canvas.h"
#include "den/gfx/text.h"
#include "den/ui/glyphs.h"
#include "den/ui/page.h"

namespace den::app {

namespace {

using Point = std::pair<float, float>;

// Draws in a square of side `s` at x, y; points are 0..1 of it.
struct Pen {
    ui::Context& ctx;
    float x, y, s;
    gfx::Color ink;

    float X(float u) const {
        return x + u * s;
    }
    float Y(float v) const {
        return y + v * s;
    }
    void Lines(std::initializer_list<Point> points, float width = 0.09f) const {
        const Point* last = nullptr;
        for (const Point& p : points) {
            if (last != nullptr) {
                ui::DrawLine(*ctx.canvas, X(last->first), Y(last->second), X(p.first), Y(p.second), width * s, ink);
            }
            last = &p;
        }
    }
    void Fill(float u, float v, float w, float h, float radius = 0.04f) const {
        ctx.canvas->FillRect({X(u), Y(v), w * s, h * s}, ink, radius * s);
    }
    void Stroke(float u, float v, float w, float h, float radius = 0.06f) const {
        ctx.canvas->StrokeRect({X(u), Y(v), w * s, h * s}, ink, 0.08f * s, radius * s);
    }
    void Dot(float u, float v, float r) const {
        Fill(u - r, v - r, r * 2, r * 2, r);
    }
};

std::string IconOf(const QuickAction& action) {
    return action.id == "pause" && action.label == "Resume" ? std::string("play") : action.id;
}

gfx::Color Faded(gfx::Color color) {
    color.a = static_cast<std::uint8_t>(color.a * 0.45f);
    return color;
}

} // namespace

void DrawQuickIcon(ui::Context& ctx, std::string_view id, const gfx::Rect& box, gfx::Color ink) {
    const float s = std::min(box.w, box.h);
    const Pen p{ctx, box.CenterX() - s * 0.5f, box.CenterY() - s * 0.5f, s, ink};
    if (id == "pause") {
        p.Fill(0.27f, 0.2f, 0.15f, 0.6f);
        p.Fill(0.58f, 0.2f, 0.15f, 0.6f);
    } else if (id == "play") {
        p.Lines({{0.32f, 0.2f}, {0.32f, 0.8f}, {0.8f, 0.5f}, {0.32f, 0.2f}});
    } else if (id == "rewind") {
        p.Lines({{0.48f, 0.25f}, {0.22f, 0.5f}, {0.48f, 0.75f}});
        p.Lines({{0.78f, 0.25f}, {0.52f, 0.5f}, {0.78f, 0.75f}});
    } else if (id == "fast_forward") {
        p.Lines({{0.22f, 0.25f}, {0.48f, 0.5f}, {0.22f, 0.75f}});
        p.Lines({{0.52f, 0.25f}, {0.78f, 0.5f}, {0.52f, 0.75f}});
    } else if (id == "exit") {
        p.Stroke(0.16f, 0.16f, 0.4f, 0.68f);
        p.Lines({{0.44f, 0.5f}, {0.88f, 0.5f}});
        p.Lines({{0.74f, 0.36f}, {0.88f, 0.5f}, {0.74f, 0.64f}});
    } else if (id == "states") {
        p.Stroke(0.2f, 0.18f, 0.6f, 0.64f);
        p.Fill(0.33f, 0.18f, 0.34f, 0.2f, 0.02f);
        p.Fill(0.32f, 0.54f, 0.36f, 0.16f, 0.02f);
    } else if (id == "screen") {
        p.Stroke(0.12f, 0.2f, 0.76f, 0.5f);
        p.Lines({{0.5f, 0.7f}, {0.5f, 0.8f}});
        p.Lines({{0.32f, 0.82f}, {0.68f, 0.82f}});
    } else if (id == "swap") {
        p.Lines({{0.18f, 0.34f}, {0.82f, 0.34f}});
        p.Lines({{0.66f, 0.2f}, {0.82f, 0.34f}, {0.66f, 0.48f}});
        p.Lines({{0.82f, 0.66f}, {0.18f, 0.66f}});
        p.Lines({{0.34f, 0.52f}, {0.18f, 0.66f}, {0.34f, 0.8f}});
    } else if (id == "fps") {
        p.Fill(0.18f, 0.56f, 0.15f, 0.26f);
        p.Fill(0.425f, 0.36f, 0.15f, 0.46f);
        p.Fill(0.67f, 0.18f, 0.15f, 0.64f);
    } else if (id == "bfi") {
        p.Stroke(0.14f, 0.24f, 0.72f, 0.52f);
        p.Fill(0.14f, 0.24f, 0.36f, 0.52f);
    } else if (id == "controls" || id == "options") {
        for (const float v : {0.28f, 0.5f, 0.72f}) {
            p.Lines({{0.16f, v}, {0.84f, v}}, 0.06f);
        }
        p.Dot(0.36f, 0.28f, 0.09f);
        p.Dot(0.66f, 0.5f, 0.09f);
        p.Dot(0.46f, 0.72f, 0.09f);
    } else if (id == "buttons") {
        p.Dot(0.5f, 0.24f, 0.11f);
        p.Dot(0.24f, 0.5f, 0.11f);
        p.Dot(0.76f, 0.5f, 0.11f);
        p.Dot(0.5f, 0.76f, 0.11f);
    } else if (id == "cheats") {
        p.Lines({{0.5f, 0.14f}, {0.5f, 0.86f}}, 0.08f);
        p.Lines({{0.14f, 0.5f}, {0.86f, 0.5f}}, 0.08f);
        p.Lines({{0.3f, 0.3f}, {0.7f, 0.7f}}, 0.06f);
        p.Lines({{0.7f, 0.3f}, {0.3f, 0.7f}}, 0.06f);
    } else if (id == "apps") {
        p.Fill(0.18f, 0.18f, 0.28f, 0.28f, 0.07f);
        p.Fill(0.54f, 0.18f, 0.28f, 0.28f, 0.07f);
        p.Fill(0.18f, 0.54f, 0.28f, 0.28f, 0.07f);
        p.Fill(0.54f, 0.54f, 0.28f, 0.28f, 0.07f);
    } else if (id == "volume") {
        p.Fill(0.16f, 0.38f, 0.16f, 0.24f, 0.02f);
        p.Lines({{0.32f, 0.38f}, {0.52f, 0.2f}, {0.52f, 0.8f}, {0.32f, 0.62f}});
        p.Lines({{0.66f, 0.34f}, {0.75f, 0.5f}, {0.66f, 0.66f}});
    } else if (id == "more") {
        p.Dot(0.24f, 0.5f, 0.08f);
        p.Dot(0.5f, 0.5f, 0.08f);
        p.Dot(0.76f, 0.5f, 0.08f);
    } else {
        p.Dot(0.5f, 0.5f, 0.12f);
    }
}

QuickTile::QuickTile(const QuickAction& action, bool wide, float scale)
    : action_id_(IconOf(action)), label_(action.label), value_(action.value), wide_(wide),
      selected_(action.selected), enabled_(action.enabled), scale_(scale) {
    focusable = true;
    label = action.label;
}

ui::Size QuickTile::MeasureContent(ui::Context& ctx) {
    const float text = ctx.Text().Measure(label_, ctx.theme->text_body * scale_, gfx::FONT_BOLD);
    return wide_ ? ui::Size{text + 96 * scale_, 56 * scale_} : ui::Size{std::max(110 * scale_, text + 24), 104 * scale_};
}

void QuickTile::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    gfx::Text& t = ctx.Text();
    ctx.canvas->FillRect(rect, selected_ || focused ? theme.panel_strong : theme.panel, theme.radius);
    const gfx::Color ink = enabled_ ? theme.fg : Faded(theme.fg);
    const gfx::Color dim = enabled_ ? theme.text_dim : Faded(theme.text_dim);
    const int font = selected_ ? gfx::FONT_BOLD : gfx::FONT_REGULAR;
    const float body = theme.text_body * std::clamp(scale_, 0.85f, 1.3f);
    const float small = theme.text_small * std::clamp(scale_, 0.9f, 1.2f);
    if (wide_) {
        const float icon = std::min(rect.h * 0.56f, 34 * scale_);
        DrawQuickIcon(ctx, action_id_, {rect.x + 16, rect.CenterY() - icon * 0.5f, icon, icon}, ink);
        const float left = rect.x + 28 + icon;
        float right = rect.Right() - 16;
        if (!value_.empty()) {
            const std::string shown = t.Elide(value_, small, rect.w * 0.4f);
            const float w = t.Measure(shown, small);
            right -= w;
            ctx.canvas->DrawText(shown, right, rect.CenterY() - t.Metrics(small).line_height * 0.5f, small, dim);
            right -= 12;
        }
        const std::string shown = t.Elide(label_, body, std::max(0.0f, right - left), font);
        ctx.canvas->DrawText(shown, left, rect.CenterY() - t.Metrics(body, font).line_height * 0.5f, body, ink, font);
        return;
    }
    // The icon above, the label and the value under it, centred together.
    const float line = t.Metrics(body, font).line_height;
    const float value_line = value_.empty() ? 0 : t.Metrics(small).line_height;
    const float icon = std::clamp(std::min(rect.w * 0.5f, rect.h - line - value_line - 24) * 0.7f, 16.0f, 64 * scale_);
    const float total = icon + 8 + line + value_line;
    float y = rect.CenterY() - total * 0.5f;
    DrawQuickIcon(ctx, action_id_, {rect.CenterX() - icon * 0.5f, y, icon, icon}, ink);
    y += icon + 8;
    const std::string label = t.Elide(label_, body, rect.w - 16, font);
    ctx.canvas->DrawText(label, rect.CenterX() - t.Measure(label, body, font) * 0.5f, y, body, ink, font);
    if (!value_.empty()) {
        const std::string value = t.Elide(value_, small, rect.w - 16);
        ctx.canvas->DrawText(value, rect.CenterX() - t.Measure(value, small) * 0.5f, y + line, small, dim);
    }
}

QuickTile* AddQuickTile(ui::Node& parent, const QuickAction& action, bool wide, float scale) {
    auto* tile = parent.Emplace<QuickTile>(action, wide, scale);
    tile->id = action.node_id;
    if (action.enabled) {
        tile->on_activate = action.run;
    } else {
        tile->on_activate = [refusal = action.refusal] { CompanionStage().Toast(refusal, 5.0f); };
    }
    return tile;
}

void AddQuickCover(ui::Node& parent, const QuickContext& ctx, float cover_w, float cover_h, bool text) {
    const library::Entry* game = ctx.game;
    ui::Node* line = parent.Add(ui::MakeBox(ui::Axis::Row, 14));
    line->align = ui::Align::Center;
    if (ctx.spec.cover) {
        auto* cover = line->Emplace<CoverView>(game ? game->name : std::string("Game"),
                                               game ? game->cover : std::string(), cover_w, cover_h);
        // The title is drawn only on the plain panel shown when there is no picture at all.
        cover->overlay_title = false;
        cover->hack = game && IsHackGame(*game);
    }
    if (!text) {
        return;
    }
    ui::Node* info = line->Add(ui::MakeBox(ui::Axis::Column, 4));
    info->grow = 1;
    info->justify = ui::Align::Center;
    info->Emplace<ui::Label>(game ? game->name : std::string("Game"), Svc().theme->text_heading, gfx::FONT_BOLD);
    std::string about = game ? SystemName(game->system) : std::string();
    if (game && game->play_seconds > 0) {
        about += " · " + TrF("{} played", PlayTimeText(game->play_seconds));
    }
    if (!about.empty()) {
        info->Emplace<ui::Label>(about)->tone = ui::Tone::Dim;
    }
    info->Emplace<ui::Label>(ctx.Status(), Svc().theme->text_small, gfx::FONT_BOLD)->tone = ui::Tone::Dim;
}

} // namespace den::app
