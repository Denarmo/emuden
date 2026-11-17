// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layout_math.h"

#include <algorithm>
#include <cmath>

namespace den::app::layout {

namespace {

constexpr float HALF_PI = 1.5707964f;

} // namespace

float Rows::Extent() const {
    return count == 0 ? 0 : static_cast<float>(count) * Pitch() - gap;
}

Span Rows::Visible(float scroll, float view, std::size_t extra) const {
    if (count == 0 || view <= 0) {
        return {};
    }
    const float pitch = std::max(1.0f, Pitch());
    const float first = std::floor(std::max(0.0f, scroll) / pitch);
    const float last = std::ceil(std::max(0.0f, scroll + view) / pitch);
    const std::size_t from = static_cast<std::size_t>(first);
    const std::size_t to = std::min(count, static_cast<std::size_t>(last) + extra);
    return {std::min(from > extra ? from - extra : 0, to), to};
}

std::optional<std::size_t> Rows::At(float offset) const {
    if (offset < 0 || count == 0) {
        return std::nullopt;
    }
    const float pitch = std::max(1.0f, Pitch());
    const std::size_t index = static_cast<std::size_t>(offset / pitch);
    if (index >= count || offset - static_cast<float>(index) * pitch > row) {
        return std::nullopt;
    }
    return index;
}

float Rows::Reveal(std::size_t index, float scroll, float view, float margin) const {
    const float max = std::max(0.0f, Extent() - view);
    const float start = Start(std::min(index, count == 0 ? 0 : count - 1));
    margin = std::min(margin, std::max(0.0f, (view - row) * 0.5f));
    if (start - margin < scroll) {
        scroll = start - margin;
    } else if (start + row + margin > scroll + view) {
        scroll = start + row + margin - view;
    }
    return std::clamp(scroll, 0.0f, max);
}

int FitColumns(float width, float cell, float gap, int min) {
    if (cell <= 0) {
        return min;
    }
    return std::max(min, static_cast<int>(std::floor((width + gap) / (cell + gap))));
}

std::optional<std::size_t> GridMove(std::size_t index, std::size_t count, int columns, Move move) {
    if (count == 0 || columns < 1 || index >= count) {
        return std::nullopt;
    }
    const std::size_t cols = static_cast<std::size_t>(columns);
    switch (move) {
    case Move::Left:
        return index > 0 ? std::optional(index - 1) : std::nullopt;
    case Move::Right:
        return index + 1 < count ? std::optional(index + 1) : std::nullopt;
    case Move::Up:
        return index >= cols ? std::optional(index - cols) : std::nullopt;
    case Move::Down:
        if (index + cols < count) {
            return index + cols;
        }
        // A shorter last row below this one: its last cell.
        return index / cols < (count - 1) / cols ? std::optional(count - 1) : std::nullopt;
    }
    return std::nullopt;
}

WheelSpot WheelAt(float offset, int half, float curve) {
    const float a = offset / (static_cast<float>(std::max(1, half)) + 0.5f);
    const float along = std::fabs(a);
    curve = std::clamp(curve, 0.0f, 1.0f);
    WheelSpot spot;
    const float bent = std::sin(std::clamp(a, -1.0f, 1.0f) * HALF_PI);
    spot.y = a + (bent - a) * curve;
    spot.x = curve * 0.55f * (1 - std::cos(std::clamp(a, -1.0f, 1.0f) * HALF_PI));
    spot.scale = 1.0f - 0.2f * std::min(1.0f, std::fabs(offset)) - 0.2f * std::min(1.0f, along);
    spot.alpha = std::clamp((1.12f - along) / 0.25f, 0.0f, 1.0f);
    return spot;
}

} // namespace den::app::layout
