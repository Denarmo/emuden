// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/gfx/canvas.h"
#include "den/gfx/images.h"

using namespace den::gfx;

TEST_CASE("cover crop keeps the aspect ratio") {
    const Rect wide = CoverUv(200, 100, Rect{0, 0, 100, 100});
    CHECK(wide.w == Catch::Approx(0.5f));
    CHECK(wide.x == Catch::Approx(0.25f));
    const Rect fit = FitRect(200, 100, Rect{0, 0, 100, 100});
    CHECK(fit.h == Catch::Approx(50.0f));
    CHECK(fit.y == Catch::Approx(25.0f));
}

TEST_CASE("shrinking halves until the image fits") {
    ImageData image;
    image.width = 64;
    image.height = 32;
    image.rgba.assign(64 * 32 * 4, 200);
    ShrinkImage(image, 16);
    CHECK(image.width == 16);
    CHECK(image.height == 8);
    CHECK(image.rgba.size() == 16u * 8u * 4u);
    CHECK(image.rgba[0] == 200);
}

TEST_CASE("colour helpers") {
    const Color c = Color::Rgb(0x0000ff).WithAlpha(0.5f);
    CHECK(c.b == 255);
    CHECK(c.a == 128);
}
