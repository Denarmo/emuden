// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/player/screens.h"

namespace den::player {

namespace {

constexpr float SMALL_SHARE = 0.25f; // the least the small screen gets of the room's width

float Snap(float value, float scale) {
    return scale > 0 ? std::round(value * scale) / scale : value;
}

} // namespace

std::vector<GameScreen> ScreensOf(const std::string& system, int frame_width, int frame_height) {
    if (frame_width <= 0 || frame_height <= 0) {
        return {};
    }
    const int half = frame_height / 2;
    if (system == "nds" && frame_width * 3 == frame_height * 2) {
        // 256 x 384 or a multiple: the upper screen over the touch screen.
        return {GameScreen{gfx::Rect{0, 0, 1, 0.5f}, frame_width, half, false},
                GameScreen{gfx::Rect{0, 0.5f, 1, 0.5f}, frame_width, half, true}};
    }
    if (system == "3ds" && frame_width * 6 == frame_height * 5) {
        // 400 x 480 or a multiple: the wide screen over the touch screen, which is 320 wide
        // and centred.
        return {GameScreen{gfx::Rect{0, 0, 1, 0.5f}, frame_width, half, false},
                GameScreen{gfx::Rect{0.1f, 0.5f, 0.8f, 0.5f}, frame_width * 4 / 5, half, true}};
    }
    return {};
}

std::vector<std::pair<std::string, std::string>> ScreenOptionHints(const std::string& system) {
    if (system == "nds") {
        return {{"melonds_screen_layout", "Top/Bottom"},
                {"melonds_screen_gap", "0"},
                {"melonds_number_of_screen_layouts", "1"},
                {"melonds_screen_layout1", "top-bottom"},
                {"melonds_show_current_layout", "disabled"},
                {"desmume_screens_layout", "top/bottom"},
                {"desmume_screens_gap", "0"}};
    }
    return {};
}

gfx::Rect FitScreen(const GameScreen& screen, const gfx::Rect& box, float scale, bool integer, int align_x,
                    int align_y) {
    if (box.w <= 0 || box.h <= 0) {
        return {};
    }
    const float aspect = screen.Aspect();
    float w = box.w, h = box.w / aspect;
    if (h > box.h) {
        h = box.h;
        w = h * aspect;
    }
    if (integer && screen.width > 0 && screen.height > 0 && scale > 0) {
        const float multiple = std::floor(std::min(h * scale / static_cast<float>(screen.height),
                                                   w * scale / static_cast<float>(screen.width)) +
                                          0.01f);
        if (multiple >= 1.0f) {
            h = multiple * static_cast<float>(screen.height) / scale;
            w = multiple * static_cast<float>(screen.width) / scale;
        }
    }
    w = Snap(w, scale);
    h = Snap(h, scale);
    const float x = box.x + (box.w - w) * static_cast<float>(align_x + 1) * 0.5f;
    const float y = box.y + (box.h - h) * static_cast<float>(align_y + 1) * 0.5f;
    return gfx::Rect{Snap(x, scale), Snap(y, scale), w, h};
}

std::array<gfx::Rect, 2> LayoutScreens(const LayoutRequest& request, const GameScreen& first,
                                       const GameScreen& second) {
    const gfx::Rect& room = request.room;
    const float s = request.scale;
    const bool integer = request.integer;
    // `a` takes the leading place (upper, left, large, shown), `b` the other.
    const GameScreen& a = request.swapped ? second : first;
    const GameScreen& b = request.swapped ? first : second;
    gfx::Rect ra, rb;
    switch (request.layout) {
    case ScreenLayout::Stacked: {
        const float middle = Snap(room.y + room.h * 0.5f, s);
        ra = FitScreen(a, gfx::Rect{room.x, room.y, room.w, middle - room.y}, s, integer, 0, 1);
        rb = FitScreen(b, gfx::Rect{room.x, middle, room.w, room.Bottom() - middle}, s, integer, 0, -1);
        break;
    }
    case ScreenLayout::SideBySide: {
        const float middle = Snap(room.x + room.w * 0.5f, s);
        ra = FitScreen(a, gfx::Rect{room.x, room.y, middle - room.x, room.h}, s, integer, 1, 0);
        rb = FitScreen(b, gfx::Rect{middle, room.y, room.Right() - middle, room.h}, s, integer, -1, 0);
        break;
    }
    case ScreenLayout::LargeSmall: {
        // The large screen as high as the room, the small one in what is left beside it.
        const float most = Snap(room.w * (1.0f - SMALL_SHARE), s);
        ra = FitScreen(a, gfx::Rect{room.x, room.y, most, room.h}, s, integer, -1, 0);
        rb = FitScreen(b, gfx::Rect{ra.Right(), room.y, room.Right() - ra.Right(), room.h}, s, integer, -1, 0);
        const float shift = Snap((room.Right() - rb.Right()) * 0.5f, s);
        ra.x += shift;
        rb.x += shift;
        break;
    }
    case ScreenLayout::One:
        ra = FitScreen(a, room, s, integer);
        break;
    }
    return request.swapped ? std::array<gfx::Rect, 2>{rb, ra} : std::array<gfx::Rect, 2>{ra, rb};
}

} // namespace den::player
