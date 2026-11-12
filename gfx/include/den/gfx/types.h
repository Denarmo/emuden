// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>

namespace den::gfx {

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;

    /// 0xRRGGBB with full alpha.
    static constexpr Color Rgb(std::uint32_t hex) {
        return Color{static_cast<std::uint8_t>(hex >> 16), static_cast<std::uint8_t>(hex >> 8),
                     static_cast<std::uint8_t>(hex), 255};
    }
    constexpr Color WithAlpha(float alpha) const {
        return Color{r, g, b, static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f)};
    }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    float Right() const {
        return x + w;
    }
    float Bottom() const {
        return y + h;
    }
    float CenterX() const {
        return x + w * 0.5f;
    }
    float CenterY() const {
        return y + h * 0.5f;
    }
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    Rect Inset(float d) const {
        return Rect{x + d, y + d, w - 2 * d, h - 2 * d};
    }
    Rect Intersect(const Rect& o) const {
        const float left = std::max(x, o.x), top = std::max(y, o.y);
        const float right = std::min(Right(), o.Right()), bottom = std::min(Bottom(), o.Bottom());
        return Rect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    }
};

/// A GPU texture. id 0 is "none".
struct Texture {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
    explicit operator bool() const {
        return id != 0;
    }
};

} // namespace den::gfx
