// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/player/screens.h"

using namespace den;
using namespace den::player;
using Catch::Approx;

TEST_CASE("a Nintendo DS picture is two screens only when they are stacked") {
    const auto screens = ScreensOf("nds", 256, 384);
    REQUIRE(screens.size() == 2);
    CHECK(screens[0].part.y == 0);
    CHECK(screens[1].part.y == Approx(0.5f));
    CHECK(screens[1].height == 192);
    CHECK(!screens[0].touch);
    CHECK(screens[1].touch);
    CHECK(ScreensOf("nds", 512, 768).size() == 2);
    CHECK(ScreensOf("nds", 512, 192).empty()); // the core laid them side by side
    CHECK(ScreensOf("gba", 256, 384).empty());
    const auto wide = ScreensOf("3ds", 400, 480);
    REQUIRE(wide.size() == 2);
    CHECK(wide[1].width == 320);
    CHECK(wide[1].part.x == Approx(0.1f));
}

TEST_CASE("two screens on one display keep their shape and stay inside the room") {
    const auto screens = ScreensOf("nds", 256, 384);
    const gfx::Rect room{0, 0, 1280, 720};
    for (int layout = 0; layout < SCREEN_LAYOUTS; ++layout) {
        for (const bool swapped : {false, true}) {
            INFO(layout << " swapped " << swapped);
            const auto rects = LayoutScreens({static_cast<ScreenLayout>(layout), room, 1.5f, false, swapped},
                                             screens[0], screens[1]);
            for (const gfx::Rect& r : rects) {
                if (r.w <= 0) {
                    continue;
                }
                CHECK(r.w / r.h == Approx(4.0f / 3.0f).epsilon(0.01));
                CHECK(r.x >= -0.01f);
                CHECK(r.y >= -0.01f);
                CHECK(r.Right() <= room.Right() + 0.01f);
                CHECK(r.Bottom() <= room.Bottom() + 0.01f);
            }
            const bool one = static_cast<ScreenLayout>(layout) == ScreenLayout::One;
            CHECK((rects[swapped ? 0 : 1].w > 0) == !one);
            CHECK(rects[swapped ? 1 : 0].w > 0);
        }
    }
    const auto stacked = LayoutScreens({ScreenLayout::Stacked, room, 1, false, false}, screens[0], screens[1]);
    CHECK(stacked[0].Bottom() == Approx(stacked[1].y));
    CHECK(stacked[0].h == Approx(360));
    const auto large = LayoutScreens({ScreenLayout::LargeSmall, room, 1, false, false}, screens[0], screens[1]);
    CHECK(large[0].h == Approx(720));
    CHECK(large[1].w == Approx(320));
    CHECK(large[0].Right() == Approx(large[1].x));
    const auto swapped = LayoutScreens({ScreenLayout::LargeSmall, room, 1, false, true}, screens[0], screens[1]);
    CHECK(swapped[1].h == Approx(720));
}

TEST_CASE("whole multiples of the screen's own pixels") {
    const auto screens = ScreensOf("nds", 256, 384);
    const gfx::Rect fit = FitScreen(screens[1], gfx::Rect{0, 0, 1280, 960}, 1, true);
    CHECK(fit.w == Approx(1280));
    CHECK(fit.h == Approx(960));
    const gfx::Rect small = FitScreen(screens[0], gfx::Rect{0, 0, 1000, 700}, 1, true);
    CHECK(small.w == Approx(768));
    CHECK(small.h == Approx(576));
    CHECK(small.x == Approx(116));
}
