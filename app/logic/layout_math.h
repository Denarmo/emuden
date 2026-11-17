// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The arithmetic of the interface layouts that draw only what is on the screen (grid, list,
// wheel, hero): which rows a scroll position shows, where to scroll to keep the selection in
// view, how the pad moves through a grid, where the names on a wheel go.

#pragma once

#include <cstddef>
#include <optional>

namespace den::app::layout {

/// Rows [first, last) of a list.
struct Span {
    std::size_t first = 0, last = 0;
};

/// A column of `count` rows, each `row` long with `gap` between them.
struct Rows {
    std::size_t count = 0;
    float row = 1, gap = 0;

    float Pitch() const {
        return row + gap;
    }
    /// The whole column's length.
    float Extent() const;
    /// Where row `index` starts.
    float Start(std::size_t index) const {
        return static_cast<float>(index) * Pitch();
    }
    /// The rows a window `view` long at `scroll` shows at least in part, with `extra` more
    /// on each side.
    Span Visible(float scroll, float view, std::size_t extra = 0) const;
    /// The row at `offset` from the column's start (scroll included), or none in a gap or past
    /// the ends.
    std::optional<std::size_t> At(float offset) const;
    /// The scroll that shows row `index` whole with `margin` around it, moving as little as
    /// possible from `scroll`; clamped to the column.
    float Reveal(std::size_t index, float scroll, float view, float margin) const;
};

/// How many cells `cell` wide with `gap` between them fit in `width` (at least `min`).
int FitColumns(float width, float cell, float gap, int min = 1);

enum class Move { Left, Right, Up, Down };

/// Where a pad move takes the selection in a grid of `count` cells, `columns` per row. Left
/// and Right go on to the row before or after; Down from a row above a shorter last row lands
/// on the last cell. None when the move leaves the grid (the page then moves the focus).
std::optional<std::size_t> GridMove(std::size_t index, std::size_t count, int columns, Move move);

/// A name on the wheel `offset` places from the selected one (fractional while it turns).
struct WheelSpot {
    float y = 0;     // from the wheel's middle, in units of the wheel's half height
    float x = 0;     // inwards from the wheel's edge, in units of the wheel's half height
    float scale = 1; // the name's size relative to the selected one's
    float alpha = 1; // 0 when it is out of sight
};
/// `half` names fit above and below the middle one; `curve` 0 is a straight column, 1 a deep
/// arc.
WheelSpot WheelAt(float offset, int half, float curve);

} // namespace den::app::layout
