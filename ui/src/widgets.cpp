// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <format>

#include "den/ui/widgets.h"

namespace den::ui {

namespace {

float SizeOr(float size, float fallback) {
    return size > 0 ? size : fallback;
}

void DrawTextIn(Context& ctx, const gfx::Rect& rect, const std::string& text, float size, int font,
                gfx::Color color, Align align) {
    gfx::Text& t = ctx.Text();
    const std::string shown = t.Elide(text, size, rect.w, font);
    const float width = t.Measure(shown, size, font);
    const gfx::FontMetrics metrics = t.Metrics(size, font);
    float x = rect.x;
    if (align == Align::Center) {
        x += (rect.w - width) * 0.5f;
    } else if (align == Align::End) {
        x += rect.w - width;
    }
    ctx.canvas->DrawText(shown, x, rect.y + (rect.h - metrics.line_height) * 0.5f, size, color, font);
}

} // namespace

Label::Label(std::string text_, float size_, int font_) : text(std::move(text_)), size(size_), font(font_) {}

Size Label::MeasureContent(Context& ctx) {
    const float s = SizeOr(size, ctx.theme->text_body);
    return Size{ctx.Text().Measure(text, s, font), ctx.Text().Metrics(s, font).line_height};
}

void Label::DrawContent(Context& ctx) {
    const float s = SizeOr(size, ctx.theme->text_body);
    const gfx::Rect inner{rect.x + pad_left, rect.y + pad_top, rect.w - pad_left - pad_right,
                          rect.h - pad_top - pad_bottom};
    DrawTextIn(ctx, inner, text, s, font, tone == Tone::Dim ? ctx.theme->text_dim : ctx.theme->fg,
               text_align);
}

ImageView::ImageView(std::string path_) : path(std::move(path_)) {}

void ImageView::DrawContent(Context& ctx) {
    const gfx::Texture texture = ctx.textures ? ctx.textures->Get(path, max_side) : gfx::Texture{};
    if (!texture) {
        ctx.canvas->FillRect(rect, ctx.theme->panel, radius);
        return;
    }
    if (cover) {
        ctx.canvas->DrawImage(rect, texture, gfx::Color{255, 255, 255, 255}, radius,
                              gfx::CoverUv(texture.width, texture.height, rect));
    } else {
        ctx.canvas->DrawImage(gfx::FitRect(texture.width, texture.height, rect), texture);
    }
}

Tile::Tile(std::string title_, std::string image_path_, float image_width_, float image_height_)
    : title(std::move(title_)), image_path(std::move(image_path_)), image_width(image_width_),
      image_height(image_height_) {
    focusable = true;
    label = title;
    width = image_width;
    height = image_height + 34;
}

gfx::Rect Tile::FocusRect() const {
    const float extra = grow_ * 0.05f;
    const float dw = image_width * extra, dh = image_height * extra;
    return gfx::Rect{rect.x - dw * 0.5f, rect.y - dh * 0.5f, image_width + dw, image_height + dh};
}

void Tile::OnTick(Context& ctx) {
    grow_ = Approach(grow_, focused ? 1.0f : 0.0f, ctx.dt, 18.0f);
    if (std::fabs(grow_ - (focused ? 1.0f : 0.0f)) < 0.01f) {
        grow_ = focused ? 1.0f : 0.0f;
    }
}

void Tile::DrawContent(Context& ctx) {
    const Theme& theme = *ctx.theme;
    const gfx::Rect image = FocusRect();
    const gfx::Texture texture =
        ctx.textures && !image_path.empty() ? ctx.textures->Get(image_path) : gfx::Texture{};
    if (texture) {
        ctx.canvas->DrawImage(image, texture, gfx::Color{255, 255, 255, 255}, theme.radius,
                              gfx::CoverUv(texture.width, texture.height, image));
    } else {
        ctx.canvas->FillRect(image, theme.panel, theme.radius);
        DrawTextIn(ctx, image.Inset(12), title, theme.text_body, gfx::FONT_BOLD, theme.fg, Align::Center);
    }
    if (!badge.empty()) {
        const float w = ctx.Text().Measure(badge, theme.text_small, gfx::FONT_BOLD) + 14;
        const gfx::Rect chip{image.x + 8, image.y + 8, w, 22};
        ctx.canvas->FillRect(chip, theme.dialog, theme.radius_small);
        DrawTextIn(ctx, chip, badge, theme.text_small, gfx::FONT_BOLD, theme.fg, Align::Center);
    }
    const gfx::Rect caption{rect.x, image.Bottom() + 6, image_width, 24};
    DrawTextIn(ctx, caption, title, theme.text_small + 1, focused ? gfx::FONT_BOLD : gfx::FONT_REGULAR,
               focused ? theme.fg : theme.text_dim, Align::Start);
}

Button::Button(std::string text_, std::function<void()> action) : text(std::move(text_)) {
    focusable = true;
    label = text;
    on_activate = std::move(action);
    height = 48;
    SetPadding(18, 0);
}

Size Button::MeasureContent(Context& ctx) {
    // A little room: a width rounded to whole pixels must not cut the bold text.
    return Size{ctx.Text().Measure(text, ctx.theme->text_body, gfx::FONT_BOLD) + 2, 24};
}

void Button::DrawContent(Context& ctx) {
    const Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, selected || focused ? theme.panel_strong : theme.panel, theme.radius);
    const gfx::Rect inner{rect.x + pad_left, rect.y, rect.w - pad_left - pad_right, rect.h};
    DrawTextIn(ctx, inner, text, theme.text_body, selected || focused ? gfx::FONT_BOLD : gfx::FONT_REGULAR,
               theme.fg, centered ? Align::Center : Align::Start);
}

Row::Row(RowSpec spec_) : spec(std::move(spec_)) {
    focusable = true;
    id = spec.id;
    label = spec.label;
    height = spec.description.empty() ? 56.0f : 72.0f;
}

void Row::Step(int direction) {
    if (spec.kind == RowSpec::Kind::Choice && spec.get_index && spec.set_index && !spec.options.empty()) {
        const int count = static_cast<int>(spec.options.size());
        spec.set_index(((spec.get_index() + direction) % count + count) % count);
    } else if (spec.kind == RowSpec::Kind::Slider && spec.get_number && spec.set_number) {
        const float next = std::clamp(spec.get_number() + spec.step * direction, spec.min, spec.max);
        spec.set_number(std::round(next / spec.step) * spec.step);
    }
}

bool Row::WantsDrag(float x, float y) const {
    // A finger is wider than the track: some room around it counts.
    return spec.kind == RowSpec::Kind::Slider && spec.set_number && track_.w > 0 && y >= rect.y &&
           y <= rect.Bottom() && x >= track_.x - 20 && x <= track_.Right() + 20;
}

void Row::DragTo(float x, float) {
    if (spec.kind != RowSpec::Kind::Slider || !spec.set_number || track_.w <= 0) {
        return;
    }
    const float t = std::clamp((x - track_.x) / track_.w, 0.0f, 1.0f);
    float value = spec.min + t * (spec.max - spec.min);
    if (spec.step > 0) {
        value = spec.min + std::round((value - spec.min) / spec.step) * spec.step;
    }
    value = std::clamp(value, spec.min, spec.max);
    if (!spec.get_number || value != spec.get_number()) {
        spec.set_number(value);
    }
}

bool Row::OnNav(input::Nav nav) {
    using input::Nav;
    const bool adjustable = spec.kind == RowSpec::Kind::Choice || spec.kind == RowSpec::Kind::Slider;
    if (adjustable && (nav == Nav::Left || nav == Nav::Right)) {
        Step(nav == Nav::Right ? 1 : -1);
        return true;
    }
    if (nav != Nav::Confirm) {
        return false;
    }
    switch (spec.kind) {
    case RowSpec::Kind::Toggle:
        if (spec.get_bool && spec.set_bool) {
            spec.set_bool(!spec.get_bool());
        }
        return true;
    case RowSpec::Kind::Choice:
        Step(1);
        return true;
    case RowSpec::Kind::Slider:
        return true;
    default:
        if (spec.action) {
            spec.action();
        }
        return true;
    }
}

void Row::DrawContent(Context& ctx) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
    const float left = rect.x + 20, right = rect.Right() - 20;
    // The value takes the width it needs, up to its share; the label gets the rest, and
    // more than its share only when the label is short. Either is cut with an ellipsis.
    const float inner = rect.w - 40;
    float value_width = std::min(rect.w * 0.45f, 360.0f);
    std::string value_text;
    if (spec.kind == RowSpec::Kind::Toggle) {
        value_width = 52;
    } else if (spec.kind != RowSpec::Kind::Slider) {
        int value_font = gfx::FONT_REGULAR;
        if (spec.kind == RowSpec::Kind::Choice) {
            const int index = spec.get_index ? spec.get_index() : 0;
            if (index >= 0 && index < static_cast<int>(spec.options.size())) {
                value_text = spec.options[static_cast<std::size_t>(index)];
            }
            value_text = focused ? "\xE2\x80\xB9  " + value_text + "  \xE2\x80\xBA" : value_text;
            value_font = gfx::FONT_BOLD;
        } else if (spec.value) {
            value_text = spec.value();
        }
        const float wanted =
            value_text.empty() ? 0.0f : ctx.Text().Measure(value_text, theme.text_body, value_font) + 2;
        const float label_width = ctx.Text().Measure(spec.label, theme.text_body) + 2;
        value_width = std::min(wanted, std::max(value_width, inner - 12 - label_width));
    }
    const gfx::Rect text_area{left, rect.y, std::max(0.0f, inner - value_width - (value_width > 0 ? 12 : 0)),
                              rect.h};
    if (spec.description.empty()) {
        DrawTextIn(ctx, text_area, spec.label, theme.text_body, gfx::FONT_REGULAR, theme.fg, Align::Start);
    } else {
        DrawTextIn(ctx, gfx::Rect{left, rect.y + 10, text_area.w, 26}, spec.label, theme.text_body,
                   gfx::FONT_REGULAR, theme.fg, Align::Start);
        DrawTextIn(ctx, gfx::Rect{left, rect.y + 38, text_area.w, 22},
                   spec.live_description ? spec.live_description() : spec.description,
                   theme.text_small, gfx::FONT_REGULAR, theme.text_dim, Align::Start);
    }
    const gfx::Rect value_area{right - value_width, rect.y, value_width, rect.h};
    switch (spec.kind) {
    case RowSpec::Kind::Toggle: {
        const bool on = spec.get_bool && spec.get_bool();
        const gfx::Rect track{right - 52, rect.CenterY() - 14, 52, 28};
        canvas.FillRect(track, on ? theme.fg : theme.panel_strong, 14);
        const float knob_x = on ? track.Right() - 25 : track.x + 3;
        canvas.FillRect(gfx::Rect{knob_x, track.y + 3, 22, 22}, on ? theme.page : theme.fg, 11);
        break;
    }
    case RowSpec::Kind::Choice:
        DrawTextIn(ctx, value_area, value_text, theme.text_body, gfx::FONT_BOLD, theme.fg, Align::End);
        break;
    case RowSpec::Kind::Slider: {
        const float value = spec.get_number ? spec.get_number() : spec.min;
        const float t = spec.max > spec.min ? (value - spec.min) / (spec.max - spec.min) : 0.0f;
        const std::string text = spec.format ? spec.format(value) : std::format("{:.2f}", value);
        const float text_w = 72;
        const gfx::Rect track{value_area.x, rect.CenterY() - 4, value_area.w - text_w - 12, 8};
        track_ = track;
        const float filled = track.w * std::clamp(t, 0.0f, 1.0f);
        canvas.FillRect(track, theme.panel_strong, 4);
        canvas.FillRect(gfx::Rect{track.x, track.y, filled, track.h}, theme.fg, 4);
        // The knob a finger takes hold of.
        canvas.FillRect(gfx::Rect{track.x + filled - 9, track.CenterY() - 9, 18, 18}, theme.fg, 9);
        DrawTextIn(ctx, gfx::Rect{right - text_w, rect.y, text_w, rect.h}, text, theme.text_body,
                   gfx::FONT_BOLD, theme.fg, Align::End);
        break;
    }
    case RowSpec::Kind::Value:
    case RowSpec::Kind::Action:
        if (!value_text.empty()) {
            DrawTextIn(ctx, value_area, value_text, theme.text_body, gfx::FONT_REGULAR, theme.text_dim,
                       Align::End);
        }
        break;
    }
}

std::unique_ptr<Label> MakeHeading(std::string text) {
    auto label = std::make_unique<Label>(std::move(text), 0.0f, gfx::FONT_BOLD);
    label->size = 22;
    label->whole = true;
    return label;
}

std::unique_ptr<Node> MakeList(float gap) {
    auto node = MakeBox(Axis::Column, gap);
    node->scrollable = true;
    node->grow = 1;
    return node;
}

std::unique_ptr<Node> MakeShelf(float gap) {
    auto node = MakeBox(Axis::Row, gap);
    node->scrollable = true;
    node->snap = true; // a flicked shelf rests with a tile at its start
    node->align = Align::Start;
    return node;
}

} // namespace den::ui
