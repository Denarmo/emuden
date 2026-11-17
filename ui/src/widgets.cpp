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

} // namespace

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

std::vector<std::string> WrapText(gfx::Text& text, const std::string& in, float size, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush_word = [&] {
        if (word.empty()) {
            return;
        }
        const std::string joined = line.empty() ? word : line + " " + word;
        if (!line.empty() && text.Measure(joined, size) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = joined;
        }
        word.clear();
    };
    for (const char c : in) {
        if (c == ' ' || c == '\n') {
            flush_word();
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            }
        } else {
            word.push_back(c);
        }
    }
    flush_word();
    if (!line.empty()) {
        lines.push_back(line);
    }
    // A word wider than the width on its own (a path) is broken between characters.
    std::vector<std::string> out;
    for (const std::string& whole : lines) {
        if (text.Measure(whole, size) <= width) {
            out.push_back(whole);
            continue;
        }
        std::string piece;
        for (std::size_t i = 0; i < whole.size();) {
            std::size_t next = i + 1;
            while (next < whole.size() && (static_cast<unsigned char>(whole[next]) & 0xC0) == 0x80) {
                ++next; // the rest of a UTF-8 sequence
            }
            const std::string glyph = whole.substr(i, next - i);
            if (!piece.empty() && text.Measure(piece + glyph, size) > width) {
                out.push_back(piece);
                piece.clear();
            }
            piece += glyph;
            i = next;
        }
        if (!piece.empty()) {
            out.push_back(piece);
        }
    }
    return out;
}

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
