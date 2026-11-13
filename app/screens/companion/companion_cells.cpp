// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "companion_cells.h"

#include <algorithm>
#include <vector>

#include "den/gfx/canvas.h"
#include "den/gfx/images.h"
#include "den/gfx/text.h"
#include "den/ui/theme.h"

namespace den::app {

namespace {

// One line of text centred across `rect` with its middle at `y`; a little smaller where it
// would not fit, cut short only beyond that.
void CentredText(ui::Context& ctx, const gfx::Rect& rect, float y, const std::string& text, float size,
                 int font, gfx::Color color) {
    gfx::Text& t = ctx.Text();
    const float room = rect.w - 24;
    if (const float full = t.Measure(text, size, font); full > room && full > 0) {
        size *= std::max(0.8f, room / full);
    }
    const std::string shown = t.Elide(text, size, room, font);
    const float width = t.Measure(shown, size, font);
    ctx.canvas->DrawText(shown, rect.x + (rect.w - width) * 0.5f, y - t.Metrics(size, font).line_height * 0.5f,
                         size, color, font);
}

} // namespace

CompanionCard::CompanionCard(std::string title, std::function<void()> action) : title_(std::move(title)) {
    focusable = true;
    label = title_;
    on_activate = std::move(action);
}

ui::Size CompanionCard::MeasureContent(ui::Context& ctx) {
    return ui::Size{ctx.Text().Measure(title_, ctx.theme->text_body + 2, gfx::FONT_BOLD) + 36, 56.0f};
}

void CompanionCard::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, selected || focused ? theme.panel_strong : theme.panel, theme.radius);
    CentredText(ctx, rect, rect.CenterY(), title_, theme.text_body + 2, selected ? gfx::FONT_BOLD : gfx::FONT_REGULAR,
                theme.fg);
}

CompanionSlot::CompanionSlot(std::string title, std::string image)
    : title_(std::move(title)), image_(std::move(image)) {
    focusable = true;
    label = title_;
}

void CompanionSlot::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    constexpr float CAPTION = 30;
    const gfx::Rect area{rect.x, rect.y, rect.w, std::max(0.0f, rect.h - CAPTION)};
    ctx.canvas->FillRect(area, theme.panel, theme.radius);
    const gfx::Texture texture =
        ctx.textures && !image_.empty() ? ctx.textures->Get(image_) : gfx::Texture{};
    if (texture && texture.width > 0 && texture.height > 0) {
        // The whole picture, as large as the slot allows.
        const float fit = std::min(area.w / texture.width, area.h / texture.height);
        const float w = texture.width * fit, h = texture.height * fit;
        ctx.canvas->DrawImage({area.CenterX() - w * 0.5f, area.CenterY() - h * 0.5f, w, h}, texture,
                              gfx::Color{255, 255, 255, 255}, theme.radius);
    } else {
        CentredText(ctx, area, area.CenterY(), "Empty", theme.text_small, gfx::FONT_REGULAR, theme.text_dim);
    }
    CentredText(ctx, rect, area.Bottom() + CAPTION * 0.5f + 2, title_, theme.text_small + 1, gfx::FONT_BOLD,
                theme.fg);
}

void CompanionGrid(ui::Node& parent, int count, int columns, float gap,
                   const std::function<ui::Node*(ui::Node& row, int index)>& add) {
    for (int first = 0; first < count; first += columns) {
        ui::Node* row = parent.Add(ui::MakeBox(ui::Axis::Row, gap));
        row->grow = 1;
        for (int i = 0; i < columns; ++i) {
            // An unfilled last row keeps the cells as wide as the full rows' cells.
            ui::Node* cell = first + i < count ? add(*row, first + i) : row->Add(ui::MakeBox(ui::Axis::Row));
            cell->grow = 1;
            cell->width = 0; // the row is shared evenly, whatever the titles
        }
    }
}

} // namespace den::app
