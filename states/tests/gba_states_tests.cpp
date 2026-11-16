// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/states/gba.h"

using namespace den::states;

TEST_CASE("GBA cores that convert") {
    CHECK(CanConvertGbaState("GBA DS", "mGBA"));
    CHECK(CanConvertGbaState("mGBA", "gpSP"));
    CHECK_FALSE(CanConvertGbaState("mGBA", "mGBA"));
    CHECK_FALSE(CanConvertGbaState("VBA Next", "mGBA"));
}
