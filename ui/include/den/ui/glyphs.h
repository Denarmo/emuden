// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Button glyphs in the look of the pad in use, drawn with the canvas: no image files, and they
// follow the theme. Xbox pads get letters knocked out of filled circles, PlayStation pads the
// four shapes in rings, Nintendo pads letters in rings, the keyboard key caps.

#pragma once

#include <string>

#include "den/ui/node.h"

namespace den::ui {

/// A straight line with round ends.
void DrawLine(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float thickness, gfx::Color color);

/// What is printed on a button, for texts and screen readers: "A", "Cross", "LB", "Enter".
std::string PadButtonName(input::PadFamily family, input::PadButton button);

/// A pad button by position. `ink` is the glyph's colour; filled glyphs knock their letter out
/// in the theme's page colour. For the keyboard family it is the key that stands for the button.
float PadGlyphWidth(Context& ctx, input::PadFamily family, input::PadButton button, float height);
/// Draws with the top left corner at x, y; returns the width.
float DrawPadGlyph(Context& ctx, input::PadFamily family, input::PadButton button, float x, float y,
                   float height, gfx::Color ink);

/// The button or key that triggers `nav` on that family.
float NavGlyphWidth(Context& ctx, input::PadFamily family, input::Nav nav, float height);
float DrawNavGlyph(Context& ctx, input::PadFamily family, input::Nav nav, float x, float y, float height,
                   gfx::Color ink);

/// A hint: the glyph of `nav` and what it does. Returns the width.
float HintWidth(Context& ctx, input::PadFamily family, input::Nav nav, const std::string& text, float height);
float DrawHint(Context& ctx, input::PadFamily family, input::Nav nav, const std::string& text, float x,
               float y, float height, gfx::Color ink);

} // namespace den::ui
