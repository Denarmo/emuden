// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

#include "../src/retro_compile.h"

using namespace den::gfx;

namespace {

constexpr const char* VERTEX = R"(#version 450
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; vec4 SourceSize; } global;
layout(push_constant) uniform Push { float STRENGTH; } params;
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }
)";
constexpr const char* FRAGMENT = R"(#version 450
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; vec4 SourceSize; } global;
layout(push_constant) uniform Push { float STRENGTH; } params;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;
void main() { FragColor = texture(Source, vTexCoord) * params.STRENGTH * global.SourceSize.x; }
)";

} // namespace

TEST_CASE("a Slang pass becomes GLSL ES with full precision samplers and named uniforms") {
    const auto stages = CompileSlang(VERTEX, FRAGMENT, true);
    REQUIRE(stages);
    const std::string& fragment = stages.value().fragment;
    if (std::getenv("DEN_PRINT_GLSL")) {
        std::printf("%s\n----\n%s\n", stages.value().vertex.c_str(), fragment.c_str());
    }
    CHECK(fragment.find("#version 300 es") == 0);
    CHECK(fragment.find("highp sampler2D Source") != std::string::npos);
    CHECK(fragment.find("ra_io0") != std::string::npos);
    CHECK(stages.value().vertex.find("ra_io0") != std::string::npos);
}
