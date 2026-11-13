// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
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
                           "svideo", "vhs", "tv-night", "dotmatrix", "pocket", "lcd", "lcd-ghost", "backlight"}) {
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
