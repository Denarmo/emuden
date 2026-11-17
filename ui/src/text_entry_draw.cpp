// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// How the text entry looks: its panel with the line, and the keys, there or filling the
// other display.

#include <cmath>

#include "den/ui/glyphs.h"
#include "den/ui/selection.h"
#include "den/ui/text_entry.h"
#include "text_entry_parts.h"

namespace den::ui {

namespace {

using input::Nav;
using namespace text_entry;

constexpr float FIELD_H = 52, BUTTON_H = 48, NOTE_H = 30;

// The line: its end stays in view, with a caret after it.
void DrawField(Context& ctx, const gfx::Rect& field, const std::string& value, const std::string& placeholder,
               int max_length) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Text& text = ctx.Text();
    canvas.FillRect(field, theme.panel, theme.radius);
    const float size = theme.text_body + 2;
    const std::string count = std::to_string(Characters(value)) + "/" + std::to_string(max_length);
    const float count_w = text.Measure(count, theme.text_small);
    canvas.DrawText(count, field.Right() - 16 - count_w, field.y + 18, theme.text_small, theme.text_dim);
    const gfx::Rect line{field.x + 16, field.y, field.w - 48 - count_w, field.h};
    const float text_y = field.y + (field.h - size * 1.22f) * 0.5f;
    canvas.PushClip(line);
    const float text_w = text.Measure(value, size);
    const float text_x = line.x + std::min(0.0f, line.w - 4 - text_w);
    if (value.empty()) {
        canvas.DrawText(placeholder, line.x + 8, text_y, size, theme.text_dim);
    } else {
        canvas.DrawText(value, text_x, text_y, size, theme.fg);
    }
    if (std::fmod(ctx.time, 1.0) < 0.6) {
        canvas.FillRect(gfx::Rect{text_x + text_w + 2, field.y + 13, 2, field.h - 26}, theme.fg);
    }
    canvas.PopClip();
}

} // namespace

void TextEntry::LayoutKeys(float left, float top, float key_w, float key_h, bool far) {
    for (Key& key : keys_) {
        const gfx::Rect rect{left + static_cast<float>(key.column) * (key_w + KEY_GAP),
                             top + static_cast<float>(key.row) * (key_h + KEY_GAP),
                             key_w * static_cast<float>(key.span) + KEY_GAP * static_cast<float>(key.span - 1),
                             key_h};
        (far ? key.far : key.rect) = rect;
    }
}

void TextEntry::DrawKeyGrid(Context& ctx, bool far, float key_h) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Text& text = ctx.Text();
    // On the other display the keys are touched: the one selection stays where the pad is.
    const Key* selected = far ? nullptr : Selected();
    for (const Key& key : keys_) {
        const gfx::Rect& rect = far ? key.far : key.rect;
        // Shift held is shown inverted; the selection is the border alone.
        const bool lit = key.label == "Shift" && key.row == LAST_ROW && shift_;
        canvas.FillRect(rect, lit ? theme.fg : &key == selected ? theme.panel_strong : theme.panel,
                        theme.radius_small);
        const std::string label = Typed(key);
        const bool wide = key.row == LAST_ROW;
        const float label_size = (wide ? theme.text_small + 1 : theme.text_body + 1) * (far ? 1.25f : 1.0f);
        const float label_w = text.Measure(label, label_size, gfx::FONT_BOLD);
        canvas.DrawText(label, rect.CenterX() - label_w * 0.5f, rect.y + (key_h - label_size * 1.22f) * 0.5f,
                        label_size, lit ? theme.dialog : theme.fg, gfx::FONT_BOLD);
    }
    if (selected) {
        DrawSelectionBorder(canvas, selected->rect, theme.radius_small, ctx.time, theme.accent);
    }
}

void TextEntry::Draw(Context& ctx) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Text& text = ctx.Text();
    refused_ = std::max(0.0f, refused_ - ctx.dt);
    canvas.FillRect(gfx::Rect{0, 0, canvas.Width(), canvas.Height()}, theme.scrim);

    // Where the typing happens when it is not on the keys of this panel.
    const char* note = keys_apart_         ? "Type on the keys on the other screen"
                       : !system_          ? nullptr
                       : setup_.apart      ? "Type on the keyboard on the other screen"
                                           : nullptr;
    const float w = std::min(630.0f, canvas.Width() - 32);
    const float key_w = (w - PANEL_PAD * 2 - KEY_GAP * (COLUMNS - 1)) / COLUMNS;
    const float keys_h = KEY_H * (LAST_ROW + 1) + KEY_GAP * LAST_ROW;
    const float below = grid_ ? keys_h + 16 + 26 : BUTTON_H;
    const float h = 24 + 30 + 14 + FIELD_H + (note ? NOTE_H : 0) + 16 + below + 22;
    // A keyboard of the platform on this display covers its bottom: the panel sits in what
    // is left, so the line is never under it.
    const float covered = system_ && !setup_.apart ? setup_.system->CoveredShare() : 0.0f;
    const float room = canvas.Height() * (1.0f - std::clamp(covered, 0.0f, 0.9f));
    const gfx::Rect box{(canvas.Width() - w) * 0.5f, std::max(8.0f, (room - h) * 0.5f), w, h};
    canvas.FillRect(box, theme.dialog, theme.radius + 6);
    const float left = box.x + PANEL_PAD, inner = w - PANEL_PAD * 2;
    canvas.DrawText(text.Elide(request_.title, theme.text_heading, inner, gfx::FONT_BOLD), left, box.y + 24,
                    theme.text_heading, theme.fg, gfx::FONT_BOLD);

    const float shake = refused_ > 0 ? std::sin(refused_ * 50.0f) * 5.0f * refused_ : 0.0f;
    const gfx::Rect field{left + shake, box.y + 68, inner, FIELD_H};
    DrawField(ctx, field, text_, request_.placeholder, request_.max_length);
    float y = field.Bottom();
    if (note) {
        canvas.DrawText(note, left, y + 10, theme.text_small, theme.text_dim);
        y += NOTE_H;
    }
    y += 16;

    if (!grid_) {
        // The line alone: Cancel and Done for a finger; the pad has Back and Confirm.
        const float button_w = (inner - 16) * 0.5f;
        cancel_rect_ = gfx::Rect{left, y, button_w, BUTTON_H};
        done_rect_ = gfx::Rect{left + button_w + 16, y, button_w, BUTTON_H};
        const auto button = [&](const gfx::Rect& rect, const char* label) {
            canvas.FillRect(rect, theme.panel, theme.radius);
            const float label_w = text.Measure(label, theme.text_body, gfx::FONT_BOLD);
            canvas.DrawText(label, rect.CenterX() - label_w * 0.5f, rect.y + 12, theme.text_body, theme.fg,
                            gfx::FONT_BOLD);
        };
        button(cancel_rect_, "Cancel");
        button(done_rect_, "Done");
        return;
    }

    LayoutKeys(left, y, key_w, KEY_H, false);
    DrawKeyGrid(ctx, false, KEY_H);
    if (!ctx.show_hints) {
        return;
    }
    std::vector<std::pair<Nav, const char*>> hints;
    if (ctx.family == input::PadFamily::Keyboard) {
        hints = {{Nav::Confirm, "Done"}, {Nav::Back, "Cancel"}};
    } else {
        hints = {{Nav::Option, "Delete"}, {Nav::Extra, "Space"}, {Nav::ShoulderL, "Shift"},
                 {Nav::Menu, "Done"},     {Nav::Back, "Cancel"}};
    }
    float x = left;
    const float hints_y = y + keys_h + 16;
    for (const auto& [nav, what] : hints) {
        x += DrawHint(ctx, ctx.family, nav, what, x, hints_y, 24, theme.text_dim) + 18;
    }
}

void TextEntry::DrawKeys(Context& ctx) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(gfx::Rect{0, 0, canvas.Width(), canvas.Height()}, theme.page);
    const float margin = 28;
    const float inner = canvas.Width() - margin * 2;
    canvas.DrawText(ctx.Text().Elide(request_.title, theme.text_heading, inner, gfx::FONT_BOLD), margin, 20,
                    theme.text_heading, theme.fg, gfx::FONT_BOLD);
    const gfx::Rect field{margin, 60, inner, FIELD_H};
    DrawField(ctx, field, text_, request_.placeholder, request_.max_length);
    const float top = field.Bottom() + 20;
    const float key_w = (inner - KEY_GAP * (COLUMNS - 1)) / COLUMNS;
    const float key_h = std::clamp((canvas.Height() - top - margin - KEY_GAP * LAST_ROW) / (LAST_ROW + 1),
                                   KEY_H * 0.75f, 96.0f);
    LayoutKeys(margin, top, key_w, key_h, true);
    DrawKeyGrid(ctx, true, key_h);
}

} // namespace den::ui
