// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// How large a picture is shown inside an area: as large as its shape allows, or at a whole
// multiple of its rows when that is asked for or costs little.

#pragma once

namespace den::player {

/// Off: fill the area. Near: a whole multiple when it loses at most NEAR_WHOLE_LOSS of the
/// height (presets that draw a cell per pixel are even that way). Always: the largest whole
/// multiple that fits.
enum class WholeScale { Off, Near, Always };

constexpr float NEAR_WHOLE_LOSS = 0.10f;

struct PictureSize {
    float w = 0, h = 0;
};

/// The picture's size in the units of the area (device pixels for whole multiples to mean
/// anything). `aspect` is width over height, `rows` the picture's own height in pixels.
PictureSize FitPicture(float area_w, float area_h, float aspect, int rows, WholeScale whole);

} // namespace den::player
