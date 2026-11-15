// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>

#include <qrcodegen.hpp>

#include "model_widgets.h"

namespace den::app {

namespace {

constexpr float LINE_SPACING = 1.25f, BOX_PAD = 14;

void DrawLineIn(ui::Context& ctx, const gfx::Rect& rect, const std::string& text, float size, int font,
                gfx::Color color, bool centered) {
    const std::string shown = ctx.Text().Elide(text, size, rect.w, font);
    const float width = centered ? ctx.Text().Measure(shown, size, font) : rect.w;
    const float line = ctx.Text().Metrics(size, font).line_height;
    ctx.canvas->DrawText(shown, rect.x + (rect.w - width) * 0.5f, rect.y + (rect.h - line) * 0.5f, size, color,
                         font);
}

} // namespace

Paragraph::Paragraph(std::string text_, float wrap_width_) : text(std::move(text_)), wrap_width(wrap_width_) {}

void Paragraph::Wrap(ui::Context& ctx) {
    const float room = std::max(80.0f, wrap_width - (boxed ? BOX_PAD * 2 : 0));
    const float scale = ctx.Text().Scale();
    if (wrapped_text_ == text && wrapped_width_ == room && wrapped_scale_ == scale) {
        return;
    }
    wrapped_text_ = text;
    wrapped_width_ = room;
    wrapped_scale_ = scale;
    lines_.clear();
    const float s = size > 0 ? size : ctx.theme->text_body;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        std::string line;
        std::size_t word = start;
        while (word < end) {
            const std::size_t space = std::min(text.find(' ', word), end);
            const std::string piece = text.substr(word, space - word);
            const std::string tried = line.empty() ? piece : line + " " + piece;
            if (!line.empty() && ctx.Text().Measure(tried, s, font) > room) {
                lines_.push_back(std::move(line));
                line = piece;
            } else {
                line = tried;
            }
            word = space + 1;
        }
        lines_.push_back(std::move(line));
        start = end + 1;
    }
}

ui::Size Paragraph::MeasureContent(ui::Context& ctx) {
    Wrap(ctx);
    const float s = size > 0 ? size : ctx.theme->text_body;
    const float line = ctx.Text().Metrics(s, font).line_height * LINE_SPACING;
    const float box = boxed ? BOX_PAD * 2 : 0;
    return ui::Size{wrap_width, std::ceil(line * static_cast<float>(lines_.size())) + box};
}

void Paragraph::DrawContent(ui::Context& ctx) {
    Wrap(ctx);
    const ui::Theme& theme = *ctx.theme;
    const float s = size > 0 ? size : theme.text_body;
    const float line = ctx.Text().Metrics(s, font).line_height * LINE_SPACING;
    const float inset = boxed ? BOX_PAD : 0;
    if (boxed) {
        ctx.canvas->FillRect(rect, theme.panel, theme.radius);
    }
    const gfx::Color color = tone == ui::Tone::Dim ? theme.text_dim : theme.fg;
    float y = rect.y + inset;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        std::string text_line = lines_[i];
        if (clip && y + line > rect.Bottom() - inset) {
            break;
        }
        if (clip && y + line * 2 > rect.Bottom() - inset && i + 1 < lines_.size()) {
            // The last line that fits: as much of it as leaves room for the dots.
            const float room = rect.w - inset * 2;
            while (!text_line.empty() && ctx.Text().Measure(text_line + "...", s, font) > room) {
                const std::size_t space = text_line.rfind(' ');
                text_line.resize(space == std::string::npos ? 0 : space);
            }
            text_line += "...";
            i = lines_.size();
        }
        float x = rect.x + inset;
        if (centered) {
            x += (rect.w - inset * 2 - ctx.Text().Measure(text_line, s, font)) * 0.5f;
        }
        ctx.canvas->DrawText(text_line, x, y + (line - line / LINE_SPACING) * 0.5f, s, color, font);
        y += line;
    }
}

SearchField::SearchField() {
    focusable = true;
    height = 52;
}

void SearchField::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
    // A magnifier drawn from a ring and a handle.
    const float cx = rect.x + 30, cy = rect.CenterY() - 2;
    ctx.canvas->StrokeRect(gfx::Rect{cx - 9, cy - 9, 18, 18}, theme.text_dim, 2.5f, 9);
    ctx.canvas->FillRect(gfx::Rect{cx + 6, cy + 7, 9, 3}, theme.text_dim, 1.5f);
    const gfx::Rect area{rect.x + 56, rect.y, rect.w - 72, rect.h};
    DrawLineIn(ctx, area, text.empty() ? placeholder : text, theme.text_body, gfx::FONT_REGULAR,
               text.empty() ? theme.text_dim : theme.fg, false);
}

FilterChip::FilterChip(std::string text_) : text(std::move(text_)) {
    focusable = true;
    label = text;
    height = 44;
}

ui::Size FilterChip::MeasureContent(ui::Context& ctx) {
    return ui::Size{ctx.Text().Measure(text, ctx.theme->text_body, gfx::FONT_BOLD) + 58, 44};
}

void FilterChip::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, selected ? theme.panel_strong : theme.panel, rect.h * 0.5f);
    // The mark at the left: a ring, filled when the filter is on.
    const gfx::Rect ring{rect.x + 16, rect.CenterY() - 8, 16, 16};
    ctx.canvas->StrokeRect(ring, selected ? theme.fg : theme.text_dim, 2, 8);
    if (selected) {
        ctx.canvas->FillRect(ring.Inset(4.5f), theme.fg, 4);
    }
    const gfx::Rect area{rect.x + 42, rect.y, rect.w - 58, rect.h};
    DrawLineIn(ctx, area, text, theme.text_body, selected ? gfx::FONT_BOLD : gfx::FONT_REGULAR, theme.fg, false);
}

QrCode::QrCode(const std::string& text, float side) : side_(side) {
    try {
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        count_ = code.getSize();
        modules_.resize(static_cast<std::size_t>(count_ * count_));
        for (int y = 0; y < count_; ++y) {
            for (int x = 0; x < count_; ++x) {
                modules_[static_cast<std::size_t>(y * count_ + x)] = code.getModule(x, y);
            }
        }
    } catch (const std::exception&) {
        count_ = 0; // too long for a QR code: nothing is drawn
    }
}

ui::Size QrCode::MeasureContent(ui::Context&) {
    return count_ > 0 ? ui::Size{side_, side_} : ui::Size{0, 0};
}

void QrCode::DrawContent(ui::Context& ctx) {
    if (count_ == 0) {
        return;
    }
    const float side = std::min(rect.w, rect.h);
    ctx.canvas->FillRect({rect.x, rect.y, side, side}, gfx::Color{255, 255, 255, 255}, 8);
    // Four modules of quiet zone around the code.
    const float module = side / static_cast<float>(count_ + 8);
    const float left = rect.x + module * 4, top = rect.y + module * 4;
    for (int y = 0; y < count_; ++y) {
        for (int x = 0; x < count_; ++x) {
            if (modules_[static_cast<std::size_t>(y * count_ + x)]) {
                // A hair wider than a module, so neighbours leave no seams.
                ctx.canvas->FillRect({left + x * module, top + y * module, module + 0.5f, module + 0.5f},
                                     gfx::Color{0, 0, 0, 255});
            }
        }
    }
}

void ModelButton::SetEnabled(bool enabled) {
    enabled_ = enabled;
    focusable = enabled;
}

void ModelButton::DrawContent(ui::Context& ctx) {
    if (enabled_) {
        ui::Button::DrawContent(ctx);
        return;
    }
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, theme.panel, theme.radius);
    const gfx::Rect inner{rect.x + pad_left, rect.y, rect.w - pad_left - pad_right, rect.h};
    DrawLineIn(ctx, inner, text, theme.text_body, gfx::FONT_REGULAR, theme.text_dim, centered);
}

ProgressLine::ProgressLine(std::string text, float fraction) {
    axis = ui::Axis::Column;
    gap = 8;
    ui::Node* top = Add(ui::MakeBox(ui::Axis::Row, 12));
    label_ = top->Emplace<ui::Label>();
    label_->grow = 1;
    amount_ = top->Emplace<ui::Label>(std::string(), 0.0f, gfx::FONT_BOLD);
    amount_->width = 64;
    amount_->text_align = ui::Align::End;
    bar_ = Emplace<ProgressBar>();
    Set(text, fraction);
}

void ProgressLine::Set(const std::string& text, float fraction) {
    label_->text = text.empty() ? std::string("Working") : text;
    amount_->text = fraction < 0 ? std::string() : std::format("{}%", std::lround(std::clamp(fraction, 0.0f, 1.0f) * 100));
    bar_->fraction = fraction;
}

} // namespace den::app
