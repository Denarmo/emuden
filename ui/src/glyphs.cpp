// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/ui/glyphs.h"

namespace den::ui {

namespace {

using input::Nav;
using input::PadButton;
using input::PadFamily;

constexpr int BUTTONS = static_cast<int>(PadButton::Count);

// Rows: Xbox, PlayStation, Nintendo, Keyboard. Columns: PadButton order (by position).
constexpr const char* NAMES[4][BUTTONS] = {
    {"A", "B", "X", "Y", "Up", "Down", "Left", "Right", "LB", "RB", "LT", "RT", "LS", "RS", "Menu", "View",
     "Guide"},
    {"Cross", "Circle", "Square", "Triangle", "Up", "Down", "Left", "Right", "L1", "R1", "L2", "R2", "L3",
     "R3", "Options", "Create", "PS"},
    {"B", "A", "Y", "X", "Up", "Down", "Left", "Right", "L", "R", "ZL", "ZR", "LS", "RS", "+", "-", "Home"},
    {"Z", "X", "A", "S", "Up", "Down", "Left", "Right", "Q", "W", "", "", "", "", "Enter", "R Shift", ""},
};

struct Box {
    float x, y, h, cx, cy, line;
};

Box BoxAt(float x, float y, float h) {
    return Box{x, y, h, x + h * 0.5f, y + h * 0.5f, std::max(1.5f, h * 0.085f)};
}

gfx::Color Fade(gfx::Color color, float factor) {
    return color.WithAlpha(color.a / 255.0f * factor);
}

float TextTop(float y, float height, float size) {
    return y + (height - size * 1.22f) * 0.5f;
}

void Centered(Context& ctx, const std::string& text, float cx, float y, float height, float size,
              gfx::Color color) {
    const float w = ctx.Text().Measure(text, size, gfx::FONT_BOLD);
    ctx.canvas->DrawText(text, cx - w * 0.5f, TextTop(y, height, size), size, color, gfx::FONT_BOLD);
}

void Ring(Context& ctx, const Box& b, gfx::Color ink) {
    ctx.canvas->StrokeRect(gfx::Rect{b.x, b.y, b.h, b.h}, ink, b.line, b.h * 0.5f);
}

void Bar(Context& ctx, float cx, float cy, float w, float h, gfx::Color ink) {
    ctx.canvas->FillRect(gfx::Rect{cx - w * 0.5f, cy - h * 0.5f, w, h}, ink, std::min(w, h) * 0.5f);
}

// An arrow from the centre of a square of side `side`, pointing along (dx, dy).
void Arrow(Context& ctx, float cx, float cy, float side, float dx, float dy, float line, gfx::Color ink) {
    const float reach = side * 0.5f, head = side * 0.34f;
    const float tip_x = cx + dx * reach, tip_y = cy + dy * reach;
    DrawLine(*ctx.canvas, cx - dx * reach, cy - dy * reach, tip_x, tip_y, line, ink);
    // The head: two strokes back from the tip, a quarter turn apart.
    DrawLine(*ctx.canvas, tip_x, tip_y, tip_x - (dx + dy) * head, tip_y - (dy - dx) * head, line, ink);
    DrawLine(*ctx.canvas, tip_x, tip_y, tip_x - (dx - dy) * head, tip_y - (dy + dx) * head, line, ink);
}

void Direction(PadButton button, float& dx, float& dy) {
    dx = button == PadButton::DpadLeft ? -1.0f : button == PadButton::DpadRight ? 1.0f : 0.0f;
    dy = button == PadButton::DpadUp ? -1.0f : button == PadButton::DpadDown ? 1.0f : 0.0f;
}

bool IsDpad(PadButton button) {
    return button >= PadButton::DpadUp && button <= PadButton::DpadRight;
}

// A key: a cap with a second edge below, like a key seen from the front.
float CapWidth(Context& ctx, const std::string& text, float height) {
    return std::max(height, ctx.Text().Measure(text, height * 0.5f, gfx::FONT_BOLD) + height * 0.62f);
}

float KeyCap(Context& ctx, const std::string& text, float x, float y, float height, gfx::Color ink) {
    const Box b = BoxAt(x, y, height);
    const float width = CapWidth(ctx, text, height);
    const float radius = height * 0.24f, lip = height * 0.13f;
    ctx.canvas->StrokeRect(gfx::Rect{x, y, width, height}, ink, b.line, radius);
    ctx.canvas->StrokeRect(gfx::Rect{x, y, width, height - lip}, ink, b.line * 0.8f, radius);
    Centered(ctx, text, x + width * 0.5f, y, height - lip, height * 0.5f, ink);
    return width;
}

float ArrowKey(Context& ctx, float dx, float dy, float x, float y, float height, gfx::Color ink) {
    const Box b = BoxAt(x, y, height);
    const float lip = height * 0.13f;
    ctx.canvas->StrokeRect(gfx::Rect{x, y, height, height}, ink, b.line, height * 0.24f);
    ctx.canvas->StrokeRect(gfx::Rect{x, y, height, height - lip}, ink, b.line * 0.8f, height * 0.24f);
    Arrow(ctx, b.cx, b.cy - lip * 0.5f, height * 0.4f, dx, dy, b.line, ink);
    return height;
}

// The cross of a pad with one arm marked.
void Dpad(Context& ctx, const Box& b, PadButton button, gfx::Color ink) {
    const float arm = b.h * 0.32f, full = b.h * 0.92f, side = (full - arm) * 0.5f;
    const gfx::Color soft = Fade(ink, 0.3f);
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(gfx::Rect{b.cx - arm * 0.5f, b.cy - full * 0.5f, arm, full}, soft, 2);
    canvas.FillRect(gfx::Rect{b.cx - full * 0.5f, b.cy - arm * 0.5f, side, arm}, soft, 2);
    canvas.FillRect(gfx::Rect{b.cx + arm * 0.5f, b.cy - arm * 0.5f, side, arm}, soft, 2);
    float dx = 0, dy = 0;
    Direction(button, dx, dy);
    const float mid = (arm + side) * 0.5f;
    const float w = dx != 0 ? side : arm, h = dy != 0 ? side : arm;
    canvas.FillRect(gfx::Rect{b.cx + dx * mid - w * 0.5f, b.cy + dy * mid - h * 0.5f, w, h}, ink, 2);
}

void PlayStationShape(Context& ctx, const Box& b, PadButton button, gfx::Color ink) {
    gfx::Canvas& canvas = *ctx.canvas;
    const float h = b.h;
    switch (button) {
    case PadButton::South: {
        const float r = h * 0.19f;
        DrawLine(canvas, b.cx - r, b.cy - r, b.cx + r, b.cy + r, b.line, ink);
        DrawLine(canvas, b.cx - r, b.cy + r, b.cx + r, b.cy - r, b.line, ink);
        break;
    }
    case PadButton::East: {
        const float side = h * 0.46f;
        canvas.StrokeRect(gfx::Rect{b.cx - side * 0.5f, b.cy - side * 0.5f, side, side}, ink, b.line, side * 0.5f);
        break;
    }
    case PadButton::West: {
        const float side = h * 0.4f;
        canvas.StrokeRect(gfx::Rect{b.cx - side * 0.5f, b.cy - side * 0.5f, side, side}, ink, b.line, 1);
        break;
    }
    default: {
        const float top = b.cy - h * 0.25f, base = b.cy + h * 0.16f, half = h * 0.24f;
        DrawLine(canvas, b.cx, top, b.cx - half, base, b.line, ink);
        DrawLine(canvas, b.cx, top, b.cx + half, base, b.line, ink);
        DrawLine(canvas, b.cx - half, base, b.cx + half, base, b.line, ink);
        break;
    }
    }
}

void Face(Context& ctx, const Box& b, PadFamily family, PadButton button, gfx::Color ink) {
    const char* letter = NAMES[static_cast<int>(family)][static_cast<int>(button)];
    switch (family) {
    case PadFamily::Xbox:
        ctx.canvas->FillRect(gfx::Rect{b.x, b.y, b.h, b.h}, ink, b.h * 0.5f);
        Centered(ctx, letter, b.cx, b.y, b.h, b.h * 0.62f, ctx.theme->page.WithAlpha(ink.a / 255.0f));
        break;
    case PadFamily::PlayStation:
        ctx.canvas->StrokeRect(gfx::Rect{b.x, b.y, b.h, b.h}, Fade(ink, 0.55f), b.line * 0.8f, b.h * 0.5f);
        PlayStationShape(ctx, b, button, ink);
        break;
    default:
        Ring(ctx, b, ink);
        Centered(ctx, letter, b.cx, b.y, b.h, b.h * 0.56f, ink);
        break;
    }
}

void House(Context& ctx, const Box& b, gfx::Color ink) {
    const float h = b.h, line = b.line * 0.9f;
    DrawLine(*ctx.canvas, b.cx - h * 0.25f, b.cy - h * 0.01f, b.cx, b.cy - h * 0.25f, line, ink);
    DrawLine(*ctx.canvas, b.cx + h * 0.25f, b.cy - h * 0.01f, b.cx, b.cy - h * 0.25f, line, ink);
    ctx.canvas->StrokeRect(gfx::Rect{b.cx - h * 0.16f, b.cy - h * 0.02f, h * 0.32f, h * 0.24f}, ink, line, 1);
}

// Start, Select and Guide: the small symbols printed on or beside those buttons.
float SystemButton(Context& ctx, const Box& b, PadFamily family, PadButton button, gfx::Color ink) {
    gfx::Canvas& canvas = *ctx.canvas;
    const float h = b.h;
    if (button == PadButton::Guide) {
        Ring(ctx, b, ink);
        House(ctx, b, ink);
        return h;
    }
    const bool start = button == PadButton::Start;
    if (family == PadFamily::PlayStation) {
        // The narrow pills beside the touch pad.
        const float w = h * 0.62f, cx = b.x + w * 0.5f;
        canvas.StrokeRect(gfx::Rect{b.x, b.y, w, h}, ink, b.line, w * 0.5f);
        if (start) {
            for (int i = -1; i <= 1; ++i) {
                Bar(ctx, cx, b.cy + static_cast<float>(i) * h * 0.15f, w * 0.46f, b.line, ink);
            }
        } else {
            // Three rays, as on the Create button.
            DrawLine(canvas, cx, b.cy + h * 0.04f, cx, b.cy - h * 0.2f, b.line, ink);
            DrawLine(canvas, cx - w * 0.14f, b.cy + h * 0.12f, cx - w * 0.3f, b.cy + h * 0.2f, b.line, ink);
            DrawLine(canvas, cx + w * 0.14f, b.cy + h * 0.12f, cx + w * 0.3f, b.cy + h * 0.2f, b.line, ink);
        }
        return w;
    }
    Ring(ctx, b, ink);
    if (family == PadFamily::Nintendo) {
        Bar(ctx, b.cx, b.cy, h * 0.46f, b.line * 1.3f, ink);
        if (start) {
            Bar(ctx, b.cx, b.cy, b.line * 1.3f, h * 0.46f, ink);
        }
    } else if (start) {
        for (int i = -1; i <= 1; ++i) {
            Bar(ctx, b.cx, b.cy + static_cast<float>(i) * h * 0.15f, h * 0.42f, b.line, ink);
        }
    } else {
        const float side = h * 0.27f;
        canvas.StrokeRect(gfx::Rect{b.cx - h * 0.2f, b.cy - h * 0.2f, side, side}, ink, b.line * 0.8f, 1);
        canvas.StrokeRect(gfx::Rect{b.cx - h * 0.07f, b.cy - h * 0.07f, side, side}, ink, b.line * 0.8f, 1);
    }
    return h;
}

bool IsFace(PadButton button) {
    return button <= PadButton::North;
}

bool IsSystem(PadButton button) {
    return button == PadButton::Start || button == PadButton::Select || button == PadButton::Guide;
}

bool IsStick(PadButton button) {
    return button == PadButton::L3 || button == PadButton::R3;
}

bool IsTrigger(PadButton button) {
    return button == PadButton::L2 || button == PadButton::R2;
}

const char* Name(PadFamily family, PadButton button) {
    const int f = std::clamp(static_cast<int>(family), 0, 3);
    return button < PadButton::Count ? NAMES[f][static_cast<int>(button)] : "";
}

} // namespace

void DrawLine(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float thickness, gfx::Color color) {
    // The canvas draws rounded rectangles only: a line is a run of overlapping dots.
    const float length = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / (thickness * 0.4f))));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        canvas.FillRect(gfx::Rect{x0 + (x1 - x0) * t - thickness * 0.5f, y0 + (y1 - y0) * t - thickness * 0.5f,
                                  thickness, thickness},
                        color, thickness * 0.5f);
    }
}

std::string PadButtonName(PadFamily family, PadButton button) {
    return Name(family, button);
}

float PadGlyphWidth(Context& ctx, PadFamily family, PadButton button, float height) {
    const std::string name = Name(family, button);
    if (name.empty()) {
        return 0;
    }
    if (family == PadFamily::Keyboard) {
        return IsDpad(button) ? height : CapWidth(ctx, name, height);
    }
    if (IsFace(button) || IsDpad(button) || IsStick(button) || button == PadButton::Guide) {
        return height;
    }
    if (IsSystem(button)) {
        return family == PadFamily::PlayStation ? height * 0.62f : height;
    }
    return std::max(height * 1.35f, ctx.Text().Measure(name, height * 0.5f, gfx::FONT_BOLD) + height * 0.6f);
}

float DrawPadGlyph(Context& ctx, PadFamily family, PadButton button, float x, float y, float height,
                   gfx::Color ink, bool pressed) {
    const std::string name = Name(family, button);
    if (name.empty()) {
        return 0;
    }
    const Box b = BoxAt(x, y, height);
    if (family == PadFamily::Keyboard) {
        if (IsDpad(button)) {
            float dx = 0, dy = 0;
            Direction(button, dx, dy);
            return ArrowKey(ctx, dx, dy, x, y, height, ink);
        }
        return KeyCap(ctx, name, x, y, height, ink);
    }
    if (IsFace(button)) {
        Face(ctx, b, family, button, ink);
        return height;
    }
    if (IsDpad(button)) {
        Dpad(ctx, b, button, ink);
        return height;
    }
    if (IsSystem(button)) {
        return SystemButton(ctx, b, family, button, ink);
    }
    if (IsStick(button)) {
        // A stick seen from above: its rim and its cap.
        Ring(ctx, b, ink);
        const float cap = height * 0.62f;
        ctx.canvas->FillRect(gfx::Rect{b.cx - cap * 0.5f, b.cy - cap * 0.5f, cap, cap}, ink, cap * 0.5f);
        Centered(ctx, button == PadButton::L3 ? "L" : "R", b.cx, y, height, height * 0.4f,
                 ctx.theme->page.WithAlpha(ink.a / 255.0f));
        return height;
    }
    // Bumpers are outlined caps, triggers filled ones.
    const float width = PadGlyphWidth(ctx, family, button, height);
    const gfx::Rect cap{x, y + height * 0.1f, width, height * 0.8f};
    if (IsTrigger(button) || pressed) {
        ctx.canvas->FillRect(cap, ink, height * 0.3f);
        Centered(ctx, name, x + width * 0.5f, y, height, height * 0.5f, ctx.theme->page.WithAlpha(ink.a / 255.0f));
    } else {
        ctx.canvas->StrokeRect(cap, ink, b.line, height * 0.3f);
        Centered(ctx, name, x + width * 0.5f, y, height, height * 0.5f, ink);
    }
    return width;
}

float NavGlyphWidth(Context& ctx, PadFamily family, Nav nav, float height) {
    const PadButton button = input::ButtonForNav(family, nav);
    if (family != PadFamily::Keyboard && button != PadButton::Count) {
        return PadGlyphWidth(ctx, family, button, height);
    }
    if (nav == Nav::Up || nav == Nav::Down || nav == Nav::Left || nav == Nav::Right) {
        return height;
    }
    return CapWidth(ctx, input::GlyphLabel(family, nav), height);
}

float DrawNavGlyph(Context& ctx, PadFamily family, Nav nav, float x, float y, float height, gfx::Color ink) {
    const PadButton button = input::ButtonForNav(family, nav);
    if (family != PadFamily::Keyboard && button != PadButton::Count) {
        return DrawPadGlyph(ctx, family, button, x, y, height, ink);
    }
    // Keys are not pad positions: a cap with the key's own name.
    switch (nav) {
    case Nav::Up:
        return ArrowKey(ctx, 0, -1, x, y, height, ink);
    case Nav::Down:
        return ArrowKey(ctx, 0, 1, x, y, height, ink);
    case Nav::Left:
        return ArrowKey(ctx, -1, 0, x, y, height, ink);
    case Nav::Right:
        return ArrowKey(ctx, 1, 0, x, y, height, ink);
    default:
        return KeyCap(ctx, input::GlyphLabel(family, nav), x, y, height, ink);
    }
}

float HintWidth(Context& ctx, PadFamily family, Nav nav, const std::string& text, float height) {
    return NavGlyphWidth(ctx, family, nav, height) + height * 0.34f +
           ctx.Text().Measure(text, ctx.theme->text_small + 1);
}

float DrawHint(Context& ctx, PadFamily family, Nav nav, const std::string& text, float x, float y,
               float height, gfx::Color ink) {
    const float size = ctx.theme->text_small + 1;
    const float glyph = DrawNavGlyph(ctx, family, nav, x, y, height, ink) + height * 0.34f;
    return glyph + ctx.canvas->DrawText(text, x + glyph, TextTop(y, height, size), size, ink);
}

} // namespace den::ui
