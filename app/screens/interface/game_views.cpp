// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_views.h"

#include <algorithm>
#include <cmath>

#include "den/app/app.h"
#include "den/app/boot.h"
#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/strings.h"
#include "den/gfx/canvas.h"

namespace den::app {

namespace {

gfx::Color Shade(gfx::Color color, float amount) {
    const auto mul = [&](std::uint8_t v) { return static_cast<std::uint8_t>(v * std::clamp(amount, 0.0f, 1.0f)); };
    return gfx::Color{mul(color.r), mul(color.g), mul(color.b), color.a};
}

// Words on up to `lines` lines of `width`; the last one elided.
std::vector<std::string> Wrap(gfx::Text& text, const std::string& value, float size, float width, int lines,
                              int font) {
    std::vector<std::string> out;
    std::string line;
    std::size_t at = 0;
    while (at < value.size()) {
        const std::size_t space = value.find(' ', at);
        const std::string word = value.substr(at, space == std::string::npos ? std::string::npos : space - at);
        at = space == std::string::npos ? value.size() : space + 1;
        const std::string tried = line.empty() ? word : line + " " + word;
        if (line.empty() || text.Measure(tried, size, font) <= width) {
            line = tried;
            continue;
        }
        if (static_cast<int>(out.size()) + 1 == lines) {
            line = tried + (at < value.size() ? " " + value.substr(at) : std::string());
            at = value.size();
            break;
        }
        out.push_back(line);
        line = word;
    }
    if (!line.empty()) {
        out.push_back(text.Elide(line, size, width, font));
    }
    return out;
}

} // namespace

std::unique_ptr<GameView> MakeGameView(const std::shared_ptr<const InterfaceSpec>& spec) {
    if (spec->layout == "grid") {
        return std::make_unique<GridView>(spec);
    }
    if (spec->layout == "list") {
        return std::make_unique<ListView>(spec);
    }
    if (spec->layout == "wheel") {
        return std::make_unique<WheelView>(spec);
    }
    if (spec->layout == "hero") {
        return std::make_unique<HeroView>(spec);
    }
    return nullptr;
}

GameView::GameView(std::shared_ptr<const InterfaceSpec> spec) : CoverCarousel(std::move(spec)) {
    scrollable = true;
    axis = ui::Axis::Column;
    spacer_ = Add(ui::MakeBox(ui::Axis::Column));
    theme_ = Svc().theme;
}

void GameView::SetNotes(std::vector<std::string> lines, std::vector<std::string> moments) {
    lines_ = std::move(lines);
    moments_ = std::move(moments);
}

void GameView::OnGamesSet() {
    lines_.clear();
    moments_.clear();
    reveal_ = true;
    touch_ = false;
    shown_ = ~std::size_t{0};
}

std::string GameView::Line(std::size_t index) const {
    if (index < lines_.size() && !lines_[index].empty()) {
        return lines_[index];
    }
    return index < games_.size() ? RecordsLine(games_[index]) : std::string();
}

std::string GameView::Moment(std::size_t index) const {
    return index < moments_.size() ? moments_[index] : std::string();
}

void GameView::SelectFromPad(std::size_t index) {
    touch_ = false;
    Select(index);
}

bool GameView::OnNav(input::Nav nav) {
    if (Launching()) {
        return true;
    }
    if (games_.empty()) {
        return false;
    }
    switch (nav) {
    case input::Nav::Left:
        return Move(layout::Move::Left);
    case input::Nav::Right:
        return Move(layout::Move::Right);
    case input::Nav::Up:
        return Move(layout::Move::Up);
    case input::Nav::Down:
        return Move(layout::Move::Down);
    default:
        return false;
    }
}

gfx::Rect GameView::FocusRect() const {
    if (games_.empty()) {
        return rect;
    }
    // Kept on the node while the selection is scrolled away, so the border does not float
    // over the tabs.
    const gfx::Rect r = SelectedRect();
    const float top = std::clamp(r.y, rect.y, rect.Bottom() - std::min(r.h, rect.h));
    const float left = std::clamp(r.x, rect.x, rect.Right() - std::min(r.w, rect.w));
    return gfx::Rect{left, top, r.w, r.h};
}

gfx::Rect GameView::LaunchRect() const {
    return launch_rect_.w > 0 ? launch_rect_ : FocusRect();
}

bool GameView::WantsDrag(float x, float y) const {
    // A tap on another game selects it; on the selected one (or its art) it starts it.
    const std::optional<std::size_t> at = GameAt(x, y);
    return at && *at != index_;
}

void GameView::DragTo(float x, float y) {
    if (Launching() || ticks_ - touch_tick_ <= 2) {
        touch_tick_ = ticks_;
        return; // the rest of a sideways drag that began on a game
    }
    touch_tick_ = ticks_;
    if (const std::optional<std::size_t> at = GameAt(x, y)) {
        touch_ = false;
        Select(*at);
    }
}

void GameView::OnTick(ui::Context& ctx) {
    CoverCarousel::OnTick(ctx);
    theme_ = ctx.theme;
    spacer_->width = axis == ui::Axis::Row ? Extent() : ui::AUTO;
    spacer_->height = axis == ui::Axis::Column ? Extent() : ui::AUTO;
    if (games_.empty() || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    // The bounds the next layout gives, so a jump now is not held by the last tab's.
    const float view = axis == ui::Axis::Row ? rect.w : rect.h;
    scroller.SetBounds(Extent() - view, view);
    if (Centred()) {
        const float target = static_cast<float>(index_) * Pitch();
        if (scroller.Dragging()) {
            touch_ = true;
        }
        if (reveal_) {
            scroller.Jump(target);
            reveal_ = false;
        } else if (touch_) {
            // A finger turns it: the game nearest the middle is the selection, and the motion
            // comes to rest on it.
            const float last = static_cast<float>(games_.size() - 1);
            Select(static_cast<std::size_t>(std::lround(std::clamp(Place(), 0.0f, last))));
            if (!scroller.Dragging() && !scroller.Moving()) {
                touch_ = false;
                scroller.ScrollTo(static_cast<float>(index_) * Pitch());
            }
        } else if (shown_ != index_) {
            scroller.ScrollTo(target);
        }
        shown_ = index_;
        return;
    }
    if (reveal_) {
        scroller.Jump(Reveal(0));
        reveal_ = false;
        shown_ = index_;
    } else if (shown_ != index_) {
        scroller.ScrollTo(Reveal(scroller.Destination()));
        shown_ = index_;
    }
}

gfx::Rect GameView::Grown(const gfx::Rect& r) const {
    const float grow_by = 1.0f + 0.16f * LaunchAmount();
    return gfx::Rect{r.CenterX() - r.w * grow_by * 0.5f, r.CenterY() - r.h * grow_by * 0.5f, r.w * grow_by,
                     r.h * grow_by};
}

gfx::Rect GameView::DrawArt(ui::Context& ctx, const library::Entry& game, const gfx::Rect& r, float light,
                            bool fit, int side) const {
    gfx::Canvas& canvas = *ctx.canvas;
    const gfx::Texture texture = game.cover.empty() ? gfx::Texture{} : ctx.textures->Get(game.cover, side);
    const float corner = std::clamp(r.w * 0.04f, 4.0f, ctx.theme->radius);
    gfx::Rect at = r;
    if (BootCarries(game.id)) {
        return at; // the boot screen draws it
    }
    if (!texture) {
        canvas.FillRect(r, Shade(color_, 0.6f * light), corner);
    } else {
        at = fit ? gfx::FitRect(texture.width, texture.height, r) : r;
        canvas.DrawImage(at, texture, Shade(gfx::Color{255, 255, 255, 255}, light), corner,
                         fit ? gfx::Rect{0, 0, 1, 1} : gfx::CoverUv(texture.width, texture.height, r));
        if (IsHackGame(game)) {
            DrawHackSticker(ctx, at, texture, true);
        }
    }
    if (!texture || game.placeholder_cover) {
        gfx::Text& text = ctx.Text();
        const float size = std::clamp(at.w * 0.1f, ctx.theme->text_small * 0.8f, ctx.theme->text_heading);
        const std::vector<std::string> lines = Wrap(text, game.name, size, at.w - 16, 3, gfx::FONT_BOLD);
        float y = at.CenterY() - size * 0.62f * static_cast<float>(lines.size());
        for (const std::string& line : lines) {
            const float w = text.Measure(line, size, gfx::FONT_BOLD);
            canvas.DrawText(line, at.CenterX() - w * 0.5f, y, size, Shade(spec_->text, light), gfx::FONT_BOLD);
            y += size * 1.24f;
        }
    }
    return at;
}

gfx::Rect GameView::DrawFeature(ui::Context& ctx, const gfx::Rect& r, bool fit) {
    const library::Entry* game = Selected();
    if (game == nullptr) {
        return r;
    }
    const std::string moment = Moment(index_);
    const std::string now = moment.empty() ? game->cover : moment;
    if (now != feature_) {
        feature_before_ = feature_;
        feature_ = now;
        feature_fade_ = 0;
    }
    feature_fade_ = ui::Approach(feature_fade_, 1, ctx.dt, 9);
    const gfx::Texture before = feature_before_.empty() || feature_fade_ > 0.98f
                                    ? gfx::Texture{}
                                    : ctx.textures->Get(feature_before_, 1024);
    if (before) {
        const gfx::Rect at = fit ? gfx::FitRect(before.width, before.height, r) : r;
        ctx.canvas->DrawImage(at, before, gfx::Color{255, 255, 255, 255}.WithAlpha(1 - feature_fade_),
                              ctx.theme->radius, fit ? gfx::Rect{0, 0, 1, 1} : gfx::CoverUv(before.width, before.height, r));
    }
    gfx::Rect at = Grown(r);
    if (!moment.empty()) {
        if (const gfx::Texture shot = ctx.textures->Get(moment, 1024)) {
            at = fit ? gfx::FitRect(shot.width, shot.height, at) : at;
            ctx.canvas->DrawImage(at, shot, gfx::Color{255, 255, 255, 255}.WithAlpha(feature_fade_), ctx.theme->radius,
                                  fit ? gfx::Rect{0, 0, 1, 1} : gfx::CoverUv(shot.width, shot.height, at));
        }
    } else {
        at = DrawArt(ctx, *game, at, feature_fade_, fit, 1024);
    }
    launch_rect_ = at;
    return at;
}

float GameView::DrawDetails(ui::Context& ctx, const gfx::Rect& r, bool centred, int lines, float name_size) {
    const library::Entry* game = Selected();
    if (game == nullptr) {
        return 0;
    }
    gfx::Text& text = ctx.Text();
    gfx::Canvas& canvas = *ctx.canvas;
    const ui::Theme& theme = *ctx.theme;
    float y = r.y;
    const auto put = [&](const std::string& line, float size, gfx::Color color, int font) {
        const std::string shown = text.Elide(line, size, r.w, font);
        const float w = text.Measure(shown, size, font);
        canvas.DrawText(shown, centred ? r.CenterX() - w * 0.5f : r.x, y, size, color, font);
        y += size * 1.3f;
    };
    for (const std::string& line : Wrap(text, game->name, name_size, r.w, lines, gfx::FONT_BOLD)) {
        put(line, name_size, theme.fg, gfx::FONT_BOLD);
    }
    y += theme.text_small * 0.3f;
    put(Line(index_), theme.text_body, theme.text_dim, gfx::FONT_REGULAR);
    put(TrF("home.position", index_ + 1, games_.size()), theme.text_small, theme.text_dim, gfx::FONT_REGULAR);
    return y - r.y;
}

} // namespace den::app
