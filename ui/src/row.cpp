// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <format>

#include "den/ui/widgets.h"

namespace den::ui {

namespace {

constexpr float PAD = 20;      // left and right of the text
constexpr float SWITCH_W = 52; // a toggle's track
constexpr const char* ARROW_LEFT = "\xE2\x80\xB9  ";
constexpr const char* ARROW_RIGHT = "  \xE2\x80\xBA";

// Stacked rows: the lines from the top.
constexpr float TOP = 12, LINE = 26, VALUE_LINE = 28, DESC_GAP = 4, DESC_LINE = 20, BOTTOM = 12;
constexpr float PLAIN_HEIGHT = 56;

} // namespace

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

std::string Row::ValueText() const {
    if (spec.kind == RowSpec::Kind::Choice) {
        const int index = spec.get_index ? spec.get_index() : 0;
        std::string text;
        if (index >= 0 && index < static_cast<int>(spec.options.size())) {
            text = spec.options[static_cast<std::size_t>(index)];
        }
        return focused ? ARROW_LEFT + text + ARROW_RIGHT : text;
    }
    if (spec.kind == RowSpec::Kind::Value || spec.kind == RowSpec::Kind::Action) {
        return spec.value ? spec.value() : std::string();
    }
    return {};
}

Row::Stack Row::StackFor(Context& ctx, float w) const {
    const Theme& theme = *ctx.theme;
    gfx::Text& text = ctx.Text();
    const float inner = w - PAD * 2;
    Stack stack;
    if (spec.kind == RowSpec::Kind::Slider) {
        stack.value_below = true;
    } else if (spec.kind == RowSpec::Kind::Choice) {
        // By the widest option, so stepping through them does not move the row.
        float widest = 0;
        for (const std::string& option : spec.options) {
            widest = std::max(widest, text.Measure(ARROW_LEFT + option + ARROW_RIGHT, theme.text_body, gfx::FONT_BOLD));
        }
        stack.value_below = text.Measure(spec.label, theme.text_body) + 12 + widest > inner;
    } else if (spec.kind != RowSpec::Kind::Toggle) {
        const std::string value = ValueText();
        stack.value_below =
            !value.empty() && text.Measure(spec.label, theme.text_body) + 12 + text.Measure(value, theme.text_body) > inner;
    }
    // A description always gets two lines, so described rows match whatever their text.
    float bottom = TOP + LINE + (stack.value_below ? VALUE_LINE : 0.0f);
    if (!spec.description.empty()) {
        bottom += DESC_GAP + DESC_LINE * 2;
    }
    stack.height = std::max(PLAIN_HEIGHT, bottom + BOTTOM);
    return stack;
}

float Row::HeightForWidth(Context& ctx, float w) {
    if (w >= ROW_STACK_WIDTH || !ctx.theme) {
        return -1;
    }
    return StackFor(ctx, w).height;
}

void Row::DrawSlider(Context& ctx, const gfx::Rect& area, float text_y, float text_h) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    const float value = spec.get_number ? spec.get_number() : spec.min;
    const float t = spec.max > spec.min ? (value - spec.min) / (spec.max - spec.min) : 0.0f;
    const std::string text = spec.format ? spec.format(value) : std::format("{:.2f}", value);
    const float text_w = 72;
    const gfx::Rect track{area.x, area.CenterY() - 4, area.w - text_w - 12, 8};
    track_ = track;
    const float filled = track.w * std::clamp(t, 0.0f, 1.0f);
    canvas.FillRect(track, theme.panel_strong, 4);
    canvas.FillRect(gfx::Rect{track.x, track.y, filled, track.h}, theme.fg, 4);
    // The knob a finger takes hold of.
    canvas.FillRect(gfx::Rect{track.x + filled - 9, track.CenterY() - 9, 18, 18}, theme.fg, 9);
    DrawTextIn(ctx, gfx::Rect{area.Right() - text_w, text_y, text_w, text_h}, text, theme.text_body, gfx::FONT_BOLD,
               theme.fg, Align::End);
}

void Row::DrawStacked(Context& ctx) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    const Stack stack = StackFor(ctx, rect.w);
    const float left = rect.x + PAD, right = rect.Right() - PAD, inner = rect.w - PAD * 2;
    const bool toggle = spec.kind == RowSpec::Kind::Toggle;
    const bool described = !spec.description.empty();
    const std::string value = ValueText();
    const bool value_beside = !stack.value_below && !value.empty();
    const int value_font = spec.kind == RowSpec::Kind::Choice ? gfx::FONT_BOLD : gfx::FONT_REGULAR;
    const gfx::Color value_color = spec.kind == RowSpec::Kind::Choice ? theme.fg : theme.text_dim;

    // The description in at most two lines, the rest of the text cut at the end of the second.
    const float desc_w = inner - (toggle ? SWITCH_W + 12 : 0.0f);
    std::vector<std::string> lines;
    if (described) {
        const std::string text = spec.live_description ? spec.live_description() : spec.description;
        lines = WrapText(ctx.Text(), text, theme.text_small, desc_w);
        if (lines.size() > 2) {
            std::string rest = lines[1];
            for (std::size_t i = 2; i < lines.size(); ++i) {
                rest += " " + lines[i];
            }
            lines.resize(2);
            lines[1] = rest;
        }
    }
    // Two lines are kept for every description; with one, the text sits in the middle.
    const float shift = described && lines.size() < 2 ? DESC_LINE * 0.5f : 0.0f;
    // One line on its own is centred like a wide row's.
    const bool single = !described && !stack.value_below;
    const gfx::Rect label_line{left, single ? rect.y : rect.y + TOP + shift, inner, single ? rect.h : LINE};

    float label_w = inner - (toggle ? SWITCH_W + 12 : 0.0f);
    if (value_beside) {
        const float value_w = std::min(inner * 0.6f, ctx.Text().Measure(value, theme.text_body, value_font) + 2);
        label_w = std::max(0.0f, inner - value_w - 12);
        DrawTextIn(ctx, gfx::Rect{right - value_w, label_line.y, value_w, label_line.h}, value, theme.text_body,
                   value_font, value_color, Align::End);
    }
    DrawTextIn(ctx, gfx::Rect{left, label_line.y, label_w, label_line.h}, spec.label, theme.text_body,
               gfx::FONT_REGULAR, theme.fg, Align::Start);

    float y = rect.y + TOP + shift + LINE;
    if (stack.value_below) {
        const gfx::Rect line{left, y + 2, inner, LINE};
        if (spec.kind == RowSpec::Kind::Slider) {
            DrawSlider(ctx, line, line.y, line.h);
        } else {
            DrawTextIn(ctx, line, value, theme.text_body, value_font, value_color, Align::Start);
        }
        y += VALUE_LINE;
    }
    y += DESC_GAP;
    for (const std::string& line : lines) {
        DrawTextIn(ctx, gfx::Rect{left, y, desc_w, DESC_LINE}, line, theme.text_small, gfx::FONT_REGULAR,
                   theme.text_dim, Align::Start);
        y += DESC_LINE;
    }
    if (toggle) {
        const bool on = spec.get_bool && spec.get_bool();
        const gfx::Rect track{right - SWITCH_W, rect.CenterY() - 14, SWITCH_W, 28};
        canvas.FillRect(track, on ? theme.fg : theme.panel_strong, 14);
        const float knob_x = on ? track.Right() - 25 : track.x + 3;
        canvas.FillRect(gfx::Rect{knob_x, track.y + 3, 22, 22}, on ? theme.page : theme.fg, 11);
    }
}

void Row::DrawContent(Context& ctx) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
    if (rect.w < ROW_STACK_WIDTH) {
        DrawStacked(ctx);
        return;
    }
    const float left = rect.x + PAD, right = rect.Right() - PAD;
    // The value takes the width it needs, up to its share; the label gets the rest, and
    // more than its share only when the label is short. Either is cut with an ellipsis.
    const float inner = rect.w - PAD * 2;
    float value_width = std::min(rect.w * 0.45f, 360.0f);
    const std::string value_text = ValueText();
    if (spec.kind == RowSpec::Kind::Toggle) {
        value_width = SWITCH_W;
    } else if (spec.kind != RowSpec::Kind::Slider) {
        const int value_font = spec.kind == RowSpec::Kind::Choice ? gfx::FONT_BOLD : gfx::FONT_REGULAR;
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
        const gfx::Rect track{right - SWITCH_W, rect.CenterY() - 14, SWITCH_W, 28};
        canvas.FillRect(track, on ? theme.fg : theme.panel_strong, 14);
        const float knob_x = on ? track.Right() - 25 : track.x + 3;
        canvas.FillRect(gfx::Rect{knob_x, track.y + 3, 22, 22}, on ? theme.page : theme.fg, 11);
        break;
    }
    case RowSpec::Kind::Choice:
        DrawTextIn(ctx, value_area, value_text, theme.text_body, gfx::FONT_BOLD, theme.fg, Align::End);
        break;
    case RowSpec::Kind::Slider:
        DrawSlider(ctx, value_area, rect.y, rect.h);
        break;
    case RowSpec::Kind::Value:
    case RowSpec::Kind::Action:
        if (!value_text.empty()) {
            DrawTextIn(ctx, value_area, value_text, theme.text_body, gfx::FONT_REGULAR, theme.text_dim,
                       Align::End);
        }
        break;
    }
}

} // namespace den::ui
