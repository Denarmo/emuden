// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "retro_preset.h"

using namespace den;
using namespace den::player;

namespace {

namespace fs = std::filesystem;

void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

fs::path Scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "den_retro_tests" / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("a #pragma parameter line gives a slider") {
    gfx::RetroParam param;
    REQUIRE(ParseRetroParam("#pragma parameter GRID \"Grid strength\" 0.5 0.0 1.0 0.05", param));
    CHECK(param.id == "GRID");
    CHECK(param.description == "Grid strength");
    CHECK(param.initial == 0.5f);
    CHECK(param.maximum == 1.0f);
    CHECK(param.step == 0.05f);
    CHECK_FALSE(ParseRetroParam("#pragma stage vertex", param));
}

TEST_CASE("a Slang preset reads its passes, stages, includes and overrides") {
    const fs::path dir = Scratch("slang");
    Write(dir / "shaders/common.inc", "#pragma parameter STRENGTH \"Strength\" 1.0 0.0 2.0 0.1\n");
    Write(dir / "shaders/a.slang", "#version 450\n#include \"common.inc\"\n#pragma name First\n"
                                   "#pragma format R16G16B16A16_SFLOAT\n"
                                   "#pragma stage vertex\nvoid main() { /*v*/ }\n"
                                   "#pragma stage fragment\nvoid main() { /*f*/ }\n");
    Write(dir / "shaders/b.slang", "#version 450\n#pragma stage vertex\nvoid main() {}\n"
                                   "#pragma stage fragment\nvoid main() {}\n");
    Write(dir / "base.slangp", "shaders = 2\nshader0 = shaders/a.slang\nfilter_linear0 = true\n"
                               "scale_type0 = source\nscale0 = 3.0\n"
                               "shader1 = \"shaders/b.slang\" # the last\nscale_type_x1 = viewport\n"
                               "scale_type_y1 = absolute\nscale_y1 = 6\n");
    Write(dir / "presets/mine.slangp", "#reference \"../base.slangp\"\nSTRENGTH = \"1.5\"\n");

    const auto spec = LoadRetroPreset(dir / "presets/mine.slangp", false);
    REQUIRE(spec);
    const gfx::RetroSpec& s = spec.value();
    CHECK(s.language == gfx::RetroLanguage::Slang);
    REQUIRE(s.passes.size() == 2);
    CHECK(s.passes[0].alias == "First");
    CHECK(s.passes[0].linear);
    CHECK(s.passes[0].float_target);
    CHECK_FALSE(s.passes[0].float32);
    CHECK(s.passes[0].factor_x == 3.0f);
    CHECK(s.passes[0].vertex.find("/*v*/") != std::string::npos);
    CHECK(s.passes[0].vertex.find("/*f*/") == std::string::npos);
    CHECK(s.passes[0].fragment.find("/*f*/") != std::string::npos);
    CHECK(s.passes[1].scale_x == gfx::RetroScale::Viewport);
    CHECK(s.passes[1].scale_y == gfx::RetroScale::Absolute);
    CHECK(s.passes[1].factor_y == 6.0f);
    REQUIRE(s.params.size() == 1);
    CHECK(s.params[0].id == "STRENGTH");
    REQUIRE(s.values.size() == 1);
    CHECK(s.values[0] == 1.5f);
}

TEST_CASE("a GLSL preset keeps the whole file for both stages") {
    const fs::path dir = Scratch("glsl");
    Write(dir / "one.glsl", "#version 130\n#pragma parameter X \"X\" 0.2 0.0 1.0 0.1\n"
                            "#if defined(VERTEX)\nvoid main() {}\n#elif defined(FRAGMENT)\nvoid main() {}\n#endif\n");
    Write(dir / "one.glslp", "shaders = 1\nshader0 = one.glsl\n");
    const auto spec = LoadRetroPreset(dir / "one.glslp", false);
    REQUIRE(spec);
    CHECK(spec.value().language == gfx::RetroLanguage::Glsl);
    REQUIRE(spec.value().passes.size() == 1);
    CHECK_FALSE(spec.value().passes[0].scale_given);
    CHECK(spec.value().passes[0].vertex.find("defined(FRAGMENT)") != std::string::npos);
    REQUIRE(spec.value().values.size() == 1);
    CHECK(spec.value().values[0] == 0.2f);
}

TEST_CASE("a preset with a missing shader fails") {
    const fs::path dir = Scratch("missing");
    Write(dir / "bad.slangp", "shaders = 1\nshader0 = nowhere.slang\n");
    CHECK_FALSE(LoadRetroPreset(dir / "bad.slangp", false));
}
