// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "face_images.h"

using namespace den::app;
using namespace den::gfx;

namespace {

ImageData MakeImage(int w, int h, Color fill) {
    ImageData im;
    im.width = w;
    im.height = h;
    im.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            im.rgba[i] = fill.r;
            im.rgba[i + 1] = fill.g;
            im.rgba[i + 2] = fill.b;
            im.rgba[i + 3] = fill.a;
        }
    }
    return im;
}

void SetPixel(ImageData& im, int x, int y, Color c) {
    const std::size_t i = (static_cast<std::size_t>(y) * im.width + x) * 4;
    im.rgba[i] = c.r;
    im.rgba[i + 1] = c.g;
    im.rgba[i + 2] = c.b;
    im.rgba[i + 3] = c.a;
}

std::uint8_t AlphaAt(const ImageData& im, int x, int y) {
    return im.rgba[(static_cast<std::size_t>(y) * im.width + x) * 4 + 3];
}

} // namespace

TEST_CASE("CleanFace clears outer ground and center spindle hole on discs") {
    ImageData im = MakeImage(96, 96, {255, 255, 255, 255});
    const float cx = 48.0f, cy = 48.0f;
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 96; ++x) {
            const float r = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
            if (r >= 9.0f && r <= 38.0f) {
                SetPixel(im, x, y, {30, 110, 210, 255});
            }
        }
    }
    REQUIRE(CleanFace(im, FaceKind::Disc));
    CHECK(im.width == im.height);
    CHECK(AlphaAt(im, 0, 0) == 0);
    CHECK(AlphaAt(im, im.width / 2, im.height / 2) == 0);
    CHECK(AlphaAt(im, im.width / 2 + im.width / 4, im.height / 2) == 255);
}

TEST_CASE("CleanFace UmdDisc extracts the circular disc from a PSP UMD caddy") {
    // Caddy photo on white ground: rounded top, squared bottom caddy corners, central white hub boss.
    ImageData caddy = MakeImage(120, 120, {255, 255, 255, 255});
    for (int y = 10; y < 110; ++y) {
        for (int x = 10; x < 110; ++x) {
            const float d = std::hypot(static_cast<float>(x) - 60.0f, static_cast<float>(y) - 57.0f);
            if (y >= 57 || d <= 50.0f) {
                SetPixel(caddy, x, y, {205, 208, 212, 255}); // opaque white/silver caddy shell
            }
            if (d <= 36.0f && d >= 10.0f) {
                SetPixel(caddy, x, y, {190, 50, 40, 255}); // printed UMD disc area
            }
        }
    }
    REQUIRE(CleanFace(caddy, FaceKind::UmdDisc));
    CHECK(caddy.width == caddy.height);
    CHECK(AlphaAt(caddy, 0, caddy.height - 1) == 0);
    CHECK(AlphaAt(caddy, caddy.width / 2, caddy.height / 2) == 0);
    CHECK(AlphaAt(caddy, caddy.width / 2 + caddy.width / 4, caddy.height / 2) == 255);

    // Bare round disc (corners outside circle are transparent): keeps disc and clears spindle hole.
    ImageData bare = MakeImage(96, 96, {255, 255, 255, 255});
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 96; ++x) {
            const float d = std::hypot(static_cast<float>(x) + 0.5f - 48.0f, static_cast<float>(y) + 0.5f - 48.0f);
            if (d >= 8.0f && d <= 38.0f) {
                SetPixel(bare, x, y, {50, 160, 90, 255});
            }
        }
    }
    REQUIRE(CleanFace(bare, FaceKind::UmdDisc));
    CHECK(bare.width == bare.height);
    CHECK(AlphaAt(bare, bare.width / 2, bare.height / 2) == 0);
    CHECK(AlphaAt(bare, bare.width / 2 + bare.width / 4, bare.height / 2) == 255);
}

TEST_CASE("CleanFace crops margins and turns horizontal spines upright") {
    ImageData spine = MakeImage(120, 40, {255, 255, 255, 255});
    for (int y = 6; y < 34; ++y) {
        for (int x = 10; x < 110; ++x) {
            SetPixel(spine, x, y, {40, 60, 140, 255});
        }
    }
    REQUIRE(CleanFace(spine, FaceKind::Spine));
    CHECK(spine.height > spine.width);
    CHECK(spine.height == 100);
    CHECK(spine.width == 28);
}

TEST_CASE("ReadCart separates full cartridge photos from bare full-bleed label scans") {
    ImageData cart = MakeImage(100, 80, {90, 92, 96, 255});
    for (int y = 14; y < 56; ++y) {
        for (int x = 18; x < 82; ++x) {
            SetPixel(cart, x, y, {220, 40, 40, 255});
        }
    }
    const CartReading full = ReadCart(cart);
    CHECK_FALSE(full.label_only);
    CHECK(full.label.w > 0.5f);
    CHECK(full.plastic.r >= 90);

    ImageData label_scan = MakeImage(100, 80, {0, 0, 0, 255});
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 100; ++x) {
            const std::uint8_t r = static_cast<std::uint8_t>((x * 255) / 99);
            const std::uint8_t g = static_cast<std::uint8_t>((y * 255) / 79);
            const std::uint8_t b = static_cast<std::uint8_t>(255 - (x * 255) / 99);
            SetPixel(label_scan, x, y, {r, g, b, 255});
        }
    }
    const CartReading sticker = ReadCart(label_scan);
    CHECK(sticker.label_only);
}
