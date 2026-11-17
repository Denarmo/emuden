// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "interface_spec.h"
#include "layout_math.h"

using namespace den;
using namespace den::app;
using namespace den::app::layout;

TEST_CASE("the grid, list, wheel and hero layouts and their options") {
    const auto dir = PathFromUtf8("/plugins/layouts");
    for (const char* name : {"grid", "list", "wheel", "hero"}) {
        CHECK(KnownLayout(name));
        const std::string json = std::string(R"({"interface": {"layout": ")") + name + R"("}})";
        auto spec = ParseInterface(json, dir, {});
        REQUIRE(spec);
        CHECK(spec->layout == name);
    }
    CHECK_FALSE(KnownLayout("coverflow"));

    auto spec = ParseInterface(R"({"interface": {
        "layout": "grid",
        "grid": {"columns": 30, "panel": false, "titles": true},
        "list": {"rows": 2, "art": false, "art_side": "left", "thumbnails": "yes"},
        "wheel": {"side": "left", "names": 8, "curve": 3},
        "hero": {"strip": 0.01}}})",
                               dir, {});
    REQUIRE(spec);
    CHECK(spec->grid.columns == 12);
    CHECK_FALSE(spec->grid.panel);
    CHECK(spec->grid.titles);
    CHECK(spec->list.rows == 5);
    CHECK_FALSE(spec->list.art);
    CHECK(spec->list.art_left);
    CHECK(spec->list.thumbnails); // not a boolean: the default
    CHECK(spec->wheel.left);
    CHECK(spec->wheel.names == 9); // odd, so one is in the middle
    CHECK(spec->wheel.curve == 1.0f);
    CHECK(spec->hero.strip == 0.14f);

    // Defaults, and 0 columns (or 1) for a grid that fits its covers.
    auto plain = ParseInterface(R"({"interface": {"layout": "list", "grid": {"columns": 1}, "wheel": {"side": "up"}}})",
                                dir, {});
    REQUIRE(plain);
    CHECK(plain->grid.columns == 2);
    CHECK(plain->grid.panel);
    CHECK(plain->list.rows == 9);
    CHECK_FALSE(plain->list.art_left);
    CHECK_FALSE(plain->wheel.left);
    CHECK(plain->hero.strip == 0.24f);
    auto zero = ParseInterface(R"({"interface": {"layout": "grid", "grid": {"columns": 0}}})", dir, {});
    REQUIRE(zero);
    CHECK(zero->grid.columns == 0);
}

TEST_CASE("a list draws only the rows in its window") {
    const Rows rows{10000, 40, 10};
    CHECK(rows.Extent() == 10000 * 50 - 10);
    Span span = rows.Visible(0, 300);
    CHECK(span.first == 0);
    CHECK(span.last == 6);
    span = rows.Visible(1225, 300, 1);
    CHECK(span.first == 23); // row 24 is the first in view, and one more each side
    CHECK(span.last == 32);
    span = rows.Visible(499'000, 1000);
    CHECK(span.last == 10000);
    CHECK(Rows{}.Visible(0, 100).last == 0);

    CHECK(rows.At(0) == 0u);
    CHECK(rows.At(75) == 1u);
    CHECK_FALSE(rows.At(45)); // in the gaps
    CHECK_FALSE(rows.At(95));
    CHECK_FALSE(rows.At(-1));
    CHECK_FALSE(rows.At(500'000));
}

TEST_CASE("the list scrolls just enough to show the selection") {
    const Rows rows{100, 40, 10};
    CHECK(rows.Reveal(0, 0, 300, 20) == 0);
    CHECK(rows.Reveal(3, 0, 300, 20) == 0);              // already in view
    CHECK(rows.Reveal(10, 0, 300, 20) == 500 + 40 + 20 - 300); // below: its end at the bottom
    CHECK(rows.Reveal(2, 400, 300, 20) == 100 - 20);           // above: its start at the top
    CHECK(rows.Reveal(99, 0, 300, 20) == rows.Extent() - 300);  // never past the end
    CHECK(Rows{3, 40, 10}.Reveal(2, 0, 300, 20) == 0);          // all of it fits
}

TEST_CASE("the pad moves through a grid row by row") {
    // 10 cells, 4 per row: 0-3, 4-7, 8-9.
    CHECK(GridMove(0, 10, 4, Move::Left) == std::nullopt);
    CHECK(GridMove(4, 10, 4, Move::Left) == 3u);
    CHECK(GridMove(3, 10, 4, Move::Right) == 4u);
    CHECK(GridMove(9, 10, 4, Move::Right) == std::nullopt);
    CHECK(GridMove(2, 10, 4, Move::Up) == std::nullopt);
    CHECK(GridMove(6, 10, 4, Move::Up) == 2u);
    CHECK(GridMove(1, 10, 4, Move::Down) == 5u);
    CHECK(GridMove(7, 10, 4, Move::Down) == 9u); // the last row is shorter
    CHECK(GridMove(9, 10, 4, Move::Down) == std::nullopt);
    CHECK(GridMove(0, 0, 4, Move::Down) == std::nullopt);

    CHECK(FitColumns(1000, 100, 20) == 8);
    CHECK(FitColumns(50, 100, 20, 2) == 2);
}

TEST_CASE("names on the wheel bend away and fade at its ends") {
    const WheelSpot middle = WheelAt(0, 4, 0.5f);
    CHECK(middle.y == 0);
    CHECK(middle.x == 0);
    CHECK(middle.scale == 1);
    CHECK(middle.alpha == 1);
    const WheelSpot below = WheelAt(2, 4, 1);
    const WheelSpot above = WheelAt(-2, 4, 1);
    CHECK(below.y > 0);
    CHECK(above.y == -below.y);
    CHECK(below.x > 0);
    CHECK(below.scale < 1);
    CHECK(WheelAt(2, 4, 0).x == 0); // a straight column
    CHECK(WheelAt(6, 4, 0.5f).alpha == 0);
    CHECK(WheelAt(4, 4, 0.5f).alpha > 0);
}
