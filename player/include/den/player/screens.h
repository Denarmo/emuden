// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Consoles whose picture holds two screens (Nintendo DS, 3DS): which part of the core's
// picture each screen is, and where the screens go on one display. Pure geometry; the
// session draws a screen with Session::DrawScreen and takes its touches with
// Session::SetScreenPointer.

#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "den/gfx/types.h"

namespace den::player {

/// One screen of the console inside the core's picture.
struct GameScreen {
    gfx::Rect part{0, 0, 1, 1}; // of the picture, 0..1, row 0 at the top
    int width = 0, height = 0;  // the console's own pixels (256 x 192)
    bool touch = false;         // presses on it reach the game
    float Aspect() const {
        return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
    }
};

/// The screens a core's picture holds when its width over height is that of `frame_width`
/// over `frame_height`; each screen's size is its part of the picture.
struct ScreenShape {
    int frame_width = 0, frame_height = 0;
    std::vector<GameScreen> screens; // `part` and `touch`; the sizes are filled in by ScreensOf
};

/// The screens of the shape the picture has (the upper screen first); empty when it has
/// none of the shapes, and the picture is one screen.
std::vector<GameScreen> ScreensOf(const std::vector<ScreenShape>& shapes, int frame_width, int frame_height);

/// The shapes of the consoles the player knows without a plugin manifest saying (DS, 3DS,
/// the GBA DS core's GBA), for an OpenRequest that brings none.
const std::vector<ScreenShape>& BuiltinScreens(const std::string& system);

/// Two screens on one display.
enum class ScreenLayout { Stacked, SideBySide, LargeSmall, One };
constexpr int SCREEN_LAYOUTS = 4;

struct LayoutRequest {
    ScreenLayout layout = ScreenLayout::Stacked;
    gfx::Rect room;        // where the screens may go, in the canvas's units
    float scale = 1;       // device pixels per unit
    bool integer = false;  // whole multiples of the screen's own lines
    bool swapped = false;  // the second screen takes the first's place (upper, left, large, shown)
};

/// A screen inside `box` at its aspect ratio, on whole device pixels. `align_x` and
/// `align_y` are -1 (left, top), 0 (centre) or 1.
gfx::Rect FitScreen(const GameScreen& screen, const gfx::Rect& box, float scale, bool integer,
                    int align_x = 0, int align_y = 0);

/// Where the two screens go; a screen that is not shown gets an empty rectangle.
std::array<gfx::Rect, 2> LayoutScreens(const LayoutRequest& request, const GameScreen& first,
                                       const GameScreen& second);

} // namespace den::player
