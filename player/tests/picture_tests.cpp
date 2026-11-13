// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "picture_fit.h"
#include "picture_presets.h"

using namespace den;
using namespace den::player;

namespace {

const PresetData* Find(const Catalogue& catalogue, const std::string& id) {
    const auto it = std::find_if(catalogue.presets.begin(), catalogue.presets.end(),
                                 [&](const PresetData& preset) { return preset.info.id == id; });
    return it == catalogue.presets.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("the shipped picture presets load and every pass has its shader") {
    const std::filesystem::path dir = PathFromUtf8(DEN_SHADER_DIR);
    const Catalogue catalogue = LoadCatalogue(dir);
    REQUIRE(!catalogue.presets.empty());
    CHECK(catalogue.presets.front().info.id == "plain");
    for (const char* id : {"pixel", "sharp", "smooth", "scanlines", "crt", "crt-bloom", "arcade", "composite",
                           "svideo", "vhs", "tv-night", "dotmatrix", "pocket", "lcd", "lcd-ghost", "backlight", "reflective"}) {
        INFO(id);
        const PresetData* preset = Find(catalogue, id);
        REQUIRE(preset);
        const auto spec = BuildEffect(*preset, dir);
        REQUIRE(spec);
        CHECK(spec.value().passes.size() == preset->passes.size());
        CHECK(spec.value().params.size() == preset->uniforms.size());
        for (const gfx::EffectPass& pass : spec.value().passes) {
            CHECK(pass.fragment.find("void main()") != std::string::npos);
        }
        // Every slider starts inside its range.
        for (const PictureParam& param : preset->info.params) {
            CHECK(param.fallback >= param.min);
            CHECK(param.fallback <= param.max);
        }
    }
    // The last pass draws at the shown size; a feedback pass is never the last.
    for (const PresetData& preset : catalogue.presets) {
        if (!preset.passes.empty()) {
            CHECK(!preset.passes.back().feedback);
        }
    }
    CHECK(Find(catalogue, "pixel")->integer);
    CHECK(Find(catalogue, "pixel")->passes.empty());
    CHECK(Find(catalogue, "dotmatrix")->passes.front().feedback);
    // The presets that draw a cell per pixel ask for a whole multiple when it is near.
    for (const char* id : {"dotmatrix", "pocket", "lcd", "lcd-ghost", "backlight", "reflective"}) {
        INFO(id);
        CHECK(Find(catalogue, id)->near_integer);
        CHECK(!Find(catalogue, id)->integer);
    }
    CHECK(!Find(catalogue, "crt")->near_integer);
    // The two dot matrix presets are one shader with another palette.
    const PictureParam& green = Find(catalogue, "dotmatrix")->info.params.front();
    const PictureParam& grey = Find(catalogue, "pocket")->info.params.front();
    CHECK(green.id == "palette");
    CHECK(green.labels.size() == 4);
    CHECK(green.fallback == 1.0f);
    CHECK(grey.fallback == 3.0f);
}

TEST_CASE("a picture fills its area, or takes a whole multiple that is asked for or near") {
    // 160x144 on a 1920x1080 screen: 7.5 times; 7 times loses under a tenth.
    PictureSize size = FitPicture(1920, 1080, 160.0f / 144.0f, 144, WholeScale::Off);
    CHECK(size.h == 1080.0f);
    CHECK(size.w == 1200.0f);
    size = FitPicture(1920, 1080, 160.0f / 144.0f, 144, WholeScale::Near);
    CHECK(size.h == 1008.0f);
    CHECK(size.w == 1120.0f);
    // 240x160 there: 6.75 times; 6 times would lose more than a tenth.
    size = FitPicture(1920, 1080, 1.5f, 160, WholeScale::Near);
    CHECK(size.h == 1080.0f);
    size = FitPicture(1920, 1080, 1.5f, 160, WholeScale::Always);
    CHECK(size.h == 960.0f);
    CHECK(size.w == 1440.0f);
    // Already whole: 5, 10 and 15 times.
    for (const float height : {720.0f, 1440.0f, 2160.0f}) {
        CHECK(FitPicture(height * 16 / 9, height, 160.0f / 144.0f, 144, WholeScale::Near).h == height);
    }
    // Smaller than the picture: nothing whole to take.
    size = FitPicture(200, 100, 160.0f / 144.0f, 144, WholeScale::Always);
    CHECK(size.h == 100.0f);
    // A wide picture is bound by the width.
    size = FitPicture(1000, 1000, 2.0f, 100, WholeScale::Off);
    CHECK(size.w == 1000.0f);
    CHECK(size.h == 500.0f);
}

TEST_CASE("every console's automatic picture is a preset that exists") {
    const Catalogue catalogue = LoadCatalogue(PathFromUtf8(DEN_SHADER_DIR));
    bool game_boy = false;
    for (const auto& [system, preset] : catalogue.automatic) {
        INFO(system);
        CHECK(Find(catalogue, preset));
        game_boy = game_boy || (system == "gb" && preset == "dotmatrix");
    }
    CHECK(game_boy);
}

TEST_CASE("a folder without presets leaves the plain picture") {
    const Catalogue catalogue = LoadCatalogue(PathFromUtf8(DEN_SHADER_DIR) / "none");
    REQUIRE(catalogue.presets.size() == 1);
    CHECK(catalogue.presets.front().info.id == "plain");
}
