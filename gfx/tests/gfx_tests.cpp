// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/gfx/canvas.h"
#include "den/gfx/images.h"
#include "den/gfx/mesh_shapes.h"

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

TEST_CASE("slab UVs map front, back, and spine edges across the full 0..1 range") {
    std::vector<MeshVertex> verts;
    SlabParts parts = {Part::Gloss, Part::Back, Part::Shell, Part::Spine, Part::Shell};
    AddSlab(verts, RoundedOutline(0.7f, 1.0f, 0.02f, 0.02f), 0.1f, 0.015f, parts);
    float front_u0 = 1, front_u1 = 0, spine_u0 = 1, spine_u1 = 0;
    for (const MeshVertex& v : verts) {
        if (v.part == static_cast<float>(Part::Gloss)) {
            front_u0 = std::min(front_u0, v.u);
            front_u1 = std::max(front_u1, v.u);
        } else if (v.part == static_cast<float>(Part::Spine)) {
            spine_u0 = std::min(spine_u0, v.u);
            spine_u1 = std::max(spine_u1, v.u);
        }
    }
    CHECK(front_u0 == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(front_u1 == Catch::Approx(1.0f).margin(1e-4f));
    CHECK(spine_u0 == Catch::Approx(0.0f).margin(1e-4f));
    CHECK(spine_u1 == Catch::Approx(1.0f).margin(1e-4f));

    std::vector<MeshVertex> groove;
    AddGroove(groove, RoundedOutline(0.5f, 0.5f, 0, 0), 0.02f, 0.05f, 0, 0, Part::Photo, false, 1.0f);
    REQUIRE_FALSE(groove.empty());
    CHECK(groove.front().u == Catch::Approx(0.75f).margin(1e-4f));
    CHECK(groove.front().v == Catch::Approx(0.75f).margin(1e-4f));
}

TEST_CASE("shouldered and rounded outlines remain star-convex for slab triangle fans") {
    for (const auto& outline : {RoundedOutline(1.2f, 1.0f, 0.05f, 0.03f, 0.1f),
                                ShoulderedOutline(0.9f, 1.0f, 0.1f, 0.04f, 0.015f, 0.02f, 0.02f)}) {
        std::vector<MeshVertex> verts;
        AddSlab(verts, outline, 0.12f, 0.008f, SlabParts{});
        int front_tris = 0;
        for (std::size_t i = 0; i + 2 < verts.size(); i += 3) {
            if (verts[i].nz > 0.99f && verts[i + 1].nz > 0.99f && verts[i + 2].nz > 0.99f) {
                const float area = (verts[i + 1].x - verts[i].x) * (verts[i + 2].y - verts[i].y) -
                                   (verts[i + 1].y - verts[i].y) * (verts[i + 2].x - verts[i].x);
                CHECK(area > 0.0f);
                ++front_tris;
            }
        }
        CHECK(front_tris >= 4);
    }
}
