// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The selection border: the one place the accent colour exists. It is only ever a border
// around the current selection, never a fill.

#include <cmath>

#include "den/ui/selection.h"

namespace den::ui {

namespace {
constexpr gfx::Color ACCENT = gfx::Color::Rgb(0x00ffa7);
}

void DrawSelectionBorder(gfx::Canvas& canvas, const gfx::Rect& rect, float radius, double time) {
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(time * 3.2));
    const float outset = 4.0f + pulse * 1.5f;
    const gfx::Rect ring{rect.x - outset, rect.y - outset, rect.w + outset * 2, rect.h + outset * 2};
    canvas.StrokeRect(ring, ACCENT, 3.5f, radius + outset);
}

} // namespace den::ui
