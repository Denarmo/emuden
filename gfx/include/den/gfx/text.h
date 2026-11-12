// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "den/base/result.h"
#include "den/gfx/device.h"

namespace den::gfx {

/// Font ids the app registers at start: the same family in two weights.
enum FontId : int { FONT_REGULAR = 0, FONT_BOLD = 1 };

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int width = 0, height = 0; // pixels
    int left = 0, top = 0;     // bitmap offset from the pen, pixels
    float advance = 0;         // pixels
};

struct FontMetrics {
    float ascent = 0, descent = 0, line_height = 0; // logical units
};

/// Fonts rasterised with FreeType into one alpha atlas. Sizes and results are in logical
/// units; SetScale says how many pixels one unit is. Main thread only.
class Text {
public:
    Text();
    ~Text();
    Status Init(Device& device);
    /// `weight` picks an instance of a variable font (400 regular, 700 bold); ignored by
    /// static fonts. Returns the font id (0, 1, ...). Later fonts also serve as fallbacks.
    Result<int> AddFont(const std::filesystem::path& file, int weight = 400);

    void SetScale(float scale);
    float Scale() const;
    float Measure(std::string_view text, float size, int font = FONT_REGULAR);
    FontMetrics Metrics(float size, int font = FONT_REGULAR);
    /// `text` cut with an ellipsis so that it fits `max_width`.
    std::string Elide(std::string_view text, float size, float max_width, int font = FONT_REGULAR);

    /// For the canvas: the glyph of a code point at a pixel size, null when no font has it.
    const Glyph* GetGlyph(std::uint32_t code, int pixel_size, int font);
    Texture Atlas() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den::gfx
