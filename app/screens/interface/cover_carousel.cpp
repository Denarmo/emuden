// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cover_carousel.h"

#include <algorithm>
#include <cmath>

#include "den/app/boot.h"
#include "den/app/game_widgets.h"
#include "den/gfx/canvas.h"

namespace den::app {

namespace {

constexpr int REFLECTION_STRIPS = 6;
constexpr float REFLECTION_SHARE = 0.32f; // of the cover's height
constexpr int COVER_TEXTURE_SIDE = 768;

gfx::Color Shade(gfx::Color color, float amount) {
    const auto mul = [&](std::uint8_t v) { return static_cast<std::uint8_t>(v * amount); };
    return gfx::Color{mul(color.r), mul(color.g), mul(color.b), color.a};
}

} // namespace

CoverCarousel::CoverCarousel(std::shared_ptr<const InterfaceSpec> spec) : spec_(std::move(spec)) {
    focusable = true;
    grow = 1;
}

void CoverCarousel::SetGames(std::vector<library::Entry> games, std::size_t selected, float aspect,
                             gfx::Color console_color) {
    games_ = std::move(games);
    aspect_ = std::clamp(aspect, 0.4f, 2.0f);
    color_ = console_color;
    index_ = games_.empty() ? 0 : std::min(selected, games_.size() - 1);
    pos_ = static_cast<float>(index_);
    id = games_.empty() ? std::string("iface.empty") : "iface.game." + games_[index_].id;
    label = games_.empty() ? std::string() : games_[index_].name;
    OnGamesSet();
}

const library::Entry* CoverCarousel::Selected() const {
    return games_.empty() ? nullptr : &games_[index_];
}

void CoverCarousel::Select(std::size_t index, bool glide) {
    if (games_.empty()) {
        return;
    }
    index = std::min(index, games_.size() - 1);
    if (!glide) {
        pos_ = static_cast<float>(index);
    }
    if (index == index_) {
        return;
    }
    index_ = index;
    id = "iface.game." + games_[index_].id;
    label = games_[index_].name;
    if (on_select) {
        on_select(games_[index_]);
    }
}

void CoverCarousel::Launch(std::function<void()> then) {
    if (LaunchAnimationOn() && !games_.empty()) {
        NoteLaunchFrom(LaunchRect());
        then();
        return;
    }
    if (spec_->launch == "none") {
        then();
        return;
    }
    after_launch_ = std::move(then);
    launch_ = 0;
    launch_seconds_ = LaunchSeconds();
}

gfx::Rect CoverCarousel::LaunchRect() const {
    return SlotAt(static_cast<float>(index_) - pos_).rect;
}

float CoverCarousel::CoverHeight() const {
    // Room for the mirror image under the cover, and for the floating.
    const float by_height = rect.h * spec_->cover_height;
    const float by_width = rect.w * 0.42f / aspect_;
    return std::max(40.0f, std::min(by_height, by_width));
}

float CoverCarousel::Step() const {
    return CoverHeight() * aspect_ * spec_->side_scale + spec_->spacing;
}

CoverCarousel::Slot CoverCarousel::SlotAt(float offset) const {
    const float h_main = CoverHeight();
    const float w_main = h_main * aspect_;
    const float w_side = w_main * spec_->side_scale;
    const float near = w_main * 0.5f + spec_->spacing + w_side * 0.5f;
    const float a = std::fabs(offset);
    const float weight = std::max(0.0f, 1.0f - a);
    const float distance = a <= 1 ? a * near : near + (a - 1) * (w_side + spec_->spacing);
    const float cx = rect.CenterX() + (offset < 0 ? -distance : distance);
    const float scale = spec_->side_scale + (1 - spec_->side_scale) * weight;
    const float h = h_main * scale, w = w_main * scale;
    const float baseline = rect.y + spec_->bob + 10 + h_main;
    return Slot{gfx::Rect{cx - w * 0.5f, baseline - h, w, h}, weight};
}

gfx::Rect CoverCarousel::FocusRect() const {
    if (games_.empty()) {
        return rect;
    }
    return SlotAt(static_cast<float>(index_) - pos_).rect;
}

bool CoverCarousel::OnNav(input::Nav nav) {
    if (Launching()) {
        return true;
    }
    switch (nav) {
    case input::Nav::Left:
        if (index_ > 0) {
            Select(index_ - 1);
        }
        return true;
    case input::Nav::Right:
        Select(index_ + 1);
        return true;
    default:
        return false;
    }
}

bool CoverCarousel::WantsDrag(float x, float y) const {
    // A tap on the selected cover starts it; anywhere else on the row picks or swipes.
    return !games_.empty() && rect.Contains(x, y) && !FocusRect().Contains(x, y);
}

void CoverCarousel::DragTo(float x, float y) {
    if (games_.empty() || Launching()) {
        return;
    }
    if (ticks_ - drag_tick_ > 2) {
        // A new touch: the cover under it is chosen, and a swipe goes on from there.
        const float first = std::floor(pos_) - 6;
        for (float i = std::max(0.0f, first); i < pos_ + 7 && i < games_.size(); ++i) {
            if (SlotAt(i - pos_).rect.Contains(x, y)) {
                Select(static_cast<std::size_t>(i));
                break;
            }
        }
        drag_x_ = x;
        drag_pos_ = static_cast<float>(index_);
    } else {
        const float target = drag_pos_ - (x - drag_x_) / std::max(1.0f, Step());
        const float last = static_cast<float>(games_.size() - 1);
        Select(static_cast<std::size_t>(std::lround(std::clamp(target, 0.0f, last))));
    }
    drag_tick_ = ticks_;
}

void CoverCarousel::OnTick(ui::Context& ctx) {
    ++ticks_;
    time_ = ctx.time;
    pos_ = ui::Approach(pos_, static_cast<float>(index_), ctx.dt, spec_->speed);
    if (launch_ >= 0) {
        launch_ += ctx.dt / std::max(0.05f, launch_seconds_);
        if (launch_ >= 1) {
            launch_ = -1;
            if (auto then = std::move(after_launch_)) {
                then();
            }
        }
    }
}

float CoverCarousel::LaunchAmount() const {
    if (launch_ < 0) {
        return 0;
    }
    return launch_ * launch_ * (3 - 2 * launch_); // smoothstep
}

void CoverCarousel::DrawOne(ui::Context& ctx, const library::Entry& game, const Slot& slot, float lift) {
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Rect r = slot.rect;
    r.y -= lift;
    const float radius = std::max(6.0f, r.w * 0.04f);
    const float light = 1.0f - spec_->side_dim * (1.0f - slot.weight);
    const gfx::Texture texture =
        game.cover.empty() ? gfx::Texture{} : ctx.textures->Get(game.cover, COVER_TEXTURE_SIDE);

    // A soft shadow on the floor, smaller as the cover rises.
    const float shadow_w = r.w * (0.92f - lift * 0.01f);
    canvas.FillRect(gfx::Rect{r.CenterX() - shadow_w * 0.5f, slot.rect.Bottom() - 6, shadow_w, 14},
                    gfx::Color{0, 0, 0, 255}.WithAlpha(0.32f * light), 7);

    if (!texture) {
        canvas.FillRect(r, Shade(color_, 0.6f * light), radius);
    } else {
        canvas.DrawImage(r, texture, Shade(gfx::Color{255, 255, 255, 255}, light), radius,
                         gfx::CoverUv(texture.width, texture.height, r));
        if (IsHackGame(game)) {
            DrawHackSticker(ctx, r, texture, true);
        }
    }
    if (!texture || game.placeholder_cover) {
        gfx::Text& text = ctx.Text();
        const float size = std::max(14.0f, r.w * 0.09f);
        const std::string name = text.Elide(game.name, size, r.w - 24, gfx::FONT_BOLD);
        const float w = text.Measure(name, size, gfx::FONT_BOLD);
        canvas.DrawText(name, r.CenterX() - w * 0.5f, r.CenterY() - size * 0.6f, size,
                        Shade(spec_->text, light), gfx::FONT_BOLD);
    }

    // The mirror image: the cover's lower part upside down, fading in strips.
    if (spec_->reflection > 0 && texture) {
        const gfx::Rect uv = gfx::CoverUv(texture.width, texture.height, r);
        const float total = slot.rect.h * REFLECTION_SHARE;
        const float strip = total / REFLECTION_STRIPS;
        for (int k = 0; k < REFLECTION_STRIPS; ++k) {
            const float fade = 1.0f - static_cast<float>(k) / REFLECTION_STRIPS;
            const float alpha = spec_->reflection * light * fade * fade;
            const float v = uv.y + uv.h * (1.0f - REFLECTION_SHARE * k / REFLECTION_STRIPS);
            const gfx::Rect part{uv.x, v, uv.w, -uv.h * REFLECTION_SHARE / REFLECTION_STRIPS};
            canvas.DrawImage(gfx::Rect{slot.rect.x, slot.rect.Bottom() + 6 + strip * k, slot.rect.w, strip},
                             texture, gfx::Color{255, 255, 255, 255}.WithAlpha(alpha), 0, part);
        }
    }
}

void CoverCarousel::DrawContent(ui::Context& ctx) {
    if (games_.empty()) {
        return;
    }
    // Far covers first, so the selected one is drawn over its neighbours.
    std::vector<std::pair<float, std::size_t>> order;
    const float reach = rect.w * 0.5f / std::max(1.0f, Step()) + 2;
    const std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(pos_ - reach)));
    const std::size_t last = std::min(games_.size() - 1, static_cast<std::size_t>(std::ceil(pos_ + reach)));
    for (std::size_t i = first; i <= last; ++i) {
        order.emplace_back(std::fabs(static_cast<float>(i) - pos_), i);
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    const float zoom = LaunchAmount();
    const float bob = spec_->bob * (0.5f + 0.5f * static_cast<float>(std::sin(time_ * 2.2)));
    ctx.canvas->PushClip(gfx::Rect{0, rect.y, ctx.canvas->Width(), rect.h});
    for (const auto& [distance, i] : order) {
        if (BootCarries(games_[i].id)) {
            continue; // the boot screen draws it
        }
        Slot slot = SlotAt(static_cast<float>(i) - pos_);
        if (i == index_ && zoom > 0) {
            const float grow_by = 1.0f + 0.22f * zoom;
            const gfx::Rect r = slot.rect;
            slot.rect = gfx::Rect{r.CenterX() - r.w * grow_by * 0.5f, r.Bottom() - r.h * grow_by, r.w * grow_by,
                                  r.h * grow_by};
        }
        DrawOne(ctx, games_[i], slot, bob * slot.weight);
    }
    ctx.canvas->PopClip();
}

} // namespace den::app
