// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Button glyphs in the look of the pad in use: lettered circles for Xbox and Nintendo pads,
// the four shapes for PlayStation pads, key caps for the keyboard. Drawn with the canvas, so
// they follow the theme and need no image files.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "den/input/input.h"
#include "den/ui/node.h"

namespace den::app {

/// What is printed on a pad's button: "A", "LB", "Options", "ZR", "Enter".
std::string PadButtonText(input::PadFamily family, input::PadButton button);

float GlyphWidth(ui::Context& ctx, input::PadFamily family, input::PadButton button, float height);
/// Draws the glyph with its top left corner at x, y; returns its width.
float DrawGlyph(ui::Context& ctx, input::PadFamily family, input::PadButton button, float x, float y,
                float height, gfx::Color ink);

/// A row of the Buttons page: the glyph of a pad button at the left, what it does at the right.
class GlyphRow final : public ui::Node {
public:
    GlyphRow(std::string id, input::PadFamily family, input::PadButton button, std::string text);

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    std::string text_;
    input::PadFamily family_;
    input::PadButton button_;
};

/// A line of hints: a glyph and what it does, one after the other.
class HintLine final : public ui::Node {
public:
    using Item = std::pair<input::Nav, std::string>;
    HintLine(input::PadFamily family, std::vector<Item> items);

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    input::PadFamily family_;
    std::vector<Item> items_;
};

} // namespace den::app
