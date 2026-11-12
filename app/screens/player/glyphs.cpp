// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/ui/theme.h"
#include "glyphs.h"

namespace den::app {

namespace {

using input::PadButton;
using input::PadFamily;

constexpr int BUTTONS = static_cast<int>(PadButton::Count);

// Rows: Xbox, PlayStation, Nintendo, Keyboard. Columns: PadButton order (by position).
constexpr const char* TEXT[4][BUTTONS] = {
    {"A", "B", "X", "Y", "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x90", "\xE2\x86\x92", "LB", "RB", "LT",
     "RT", "LS", "RS", "Menu", "View", "Guide"},
    {"\xC3\x97", "\xE2\x97\x8B", "\xE2\x96\xA1", "\xE2\x96\xB3", "\xE2\x86\x91", "\xE2\x86\x93",
     "\xE2\x86\x90", "\xE2\x86\x92", "L1", "R1", "L2", "R2", "L3", "R3", "Options", "Share", "PS"},
    {"B", "A", "Y", "X", "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x90", "\xE2\x86\x92", "L", "R", "ZL",
     "ZR", "LS", "RS", "+", "-", "Home"},
    {"Z", "X", "A", "S", "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x90", "\xE2\x86\x92", "Q", "W", "", "",
     "", "", "Enter", "R Shift", ""},
};

bool Face(PadButton button) {
    return button <= PadButton::North;
}

// Lettered circles: the face buttons of a pad.
bool Round(PadFamily family, PadButton button) {
    return family != PadFamily::Keyboard && Face(button);
}

float TextTop(float y, float height, float size) {
    return y + (height - size * 1.22f) * 0.5f;
}

} // namespace

std::string PadButtonText(PadFamily family, PadButton button) {
    const int f = std::clamp(static_cast<int>(family), 0, 3);
    return button < PadButton::Count ? TEXT[f][static_cast<int>(button)] : "";
}

float GlyphWidth(ui::Context& ctx, PadFamily family, PadButton button, float height) {
    if (Round(family, button)) {
        return height;
    }
    const float size = height * 0.52f;
    return std::max(height, ctx.Text().Measure(PadButtonText(family, button), size, gfx::FONT_BOLD) +
                                height * 0.6f);
}

float DrawGlyph(ui::Context& ctx, PadFamily family, PadButton button, float x, float y, float height,
                gfx::Color ink) {
    gfx::Canvas& canvas = *ctx.canvas;
    const float width = GlyphWidth(ctx, family, button, height);
    const std::string text = PadButtonText(family, button);
    const float line = std::max(1.5f, height * 0.07f);
    if (Round(family, button)) {
        canvas.StrokeRect(gfx::Rect{x, y, height, height}, ink, line, height * 0.5f);
        if (family == PadFamily::PlayStation && button != PadButton::South && button != PadButton::North) {
            // Circle and square, drawn rather than typed so they sit exactly in the middle.
            const float side = height * (button == PadButton::East ? 0.46f : 0.38f);
            canvas.StrokeRect(gfx::Rect{x + (height - side) * 0.5f, y + (height - side) * 0.5f, side, side},
                              ink, line, button == PadButton::East ? side * 0.5f : 1.0f);
            return width;
        }
        const float size = height * (family == PadFamily::PlayStation ? 0.66f : 0.58f);
        const float w = ctx.Text().Measure(text, size, gfx::FONT_BOLD);
        canvas.DrawText(text, x + (height - w) * 0.5f, TextTop(y, height, size), size, ink, gfx::FONT_BOLD);
        return width;
    }
    // A cap: shoulder buttons, system buttons and keys.
    const float size = height * 0.52f;
    const float w = ctx.Text().Measure(text, size, gfx::FONT_BOLD);
    canvas.StrokeRect(gfx::Rect{x, y, width, height}, ink, line, height * 0.28f);
    canvas.DrawText(text, x + (width - w) * 0.5f, TextTop(y, height, size), size, ink, gfx::FONT_BOLD);
    return width;
}

GlyphRow::GlyphRow(std::string row_id, std::string label, PadFamily family, PadButton button,
                   std::string text)
    : label_(std::move(label)), text_(std::move(text)), family_(family), button_(button) {
    id = std::move(row_id);
    this->label = label_;
    focusable = true;
    height = 48;
}

void GlyphRow::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
    canvas.DrawText(label_, rect.x + 20, TextTop(rect.y, rect.h, theme.text_body), theme.text_body, theme.fg);
    const float right = rect.Right() - 20;
    if (button_ != PadButton::Count && !PadButtonText(family_, button_).empty()) {
        const float h = 30;
        const float w = GlyphWidth(ctx, family_, button_, h);
        DrawGlyph(ctx, family_, button_, right - w, rect.CenterY() - h * 0.5f, h, theme.fg);
    } else if (!text_.empty()) {
        const float w = ctx.Text().Measure(text_, theme.text_body, gfx::FONT_BOLD);
        canvas.DrawText(text_, right - w, TextTop(rect.y, rect.h, theme.text_body), theme.text_body,
                        theme.fg, gfx::FONT_BOLD);
    }
}

HintLine::HintLine(PadFamily family, std::vector<Item> items) : family_(family), items_(std::move(items)) {
    height = 30;
}

void HintLine::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    const float h = 24;
    float x = rect.x;
    const float y = rect.CenterY() - h * 0.5f;
    for (const auto& [nav, text] : items_) {
        const input::PadButton button = input::ButtonForNav(family_, nav);
        if (family_ == PadFamily::Keyboard || button == PadButton::Count) {
            // Keys are not pad positions: a cap with the key's own name.
            const std::string key = input::GlyphLabel(family_, nav);
            const float size = h * 0.52f;
            const float w = ctx.Text().Measure(key, size, gfx::FONT_BOLD) + h * 0.6f;
            ctx.canvas->StrokeRect(gfx::Rect{x, y, w, h}, theme.text_dim, 1.5f, h * 0.28f);
            ctx.canvas->DrawText(key, x + h * 0.3f, TextTop(y, h, size), size, theme.text_dim, gfx::FONT_BOLD);
            x += w + 8;
        } else {
            x += DrawGlyph(ctx, family_, button, x, y, h, theme.text_dim) + 8;
        }
        x += ctx.canvas->DrawText(text, x, TextTop(y, h, theme.text_small), theme.text_small, theme.text_dim) + 22;
    }
}

} // namespace den::app
