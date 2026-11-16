// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What RetroChain decides without GL: pass sizes, how textures are sampled, and which of
// RetroArch's values a shader's uniform is.

#include <catch2/catch_test_macros.hpp>

#include "../src/retro_layout.h"
#include "../src/retro_names.h"

using namespace den::gfx;

TEST_CASE("a pass without a scale is its source's size, the last one the frame's") {
    RetroPassSpec pass;
    RetroPassSize size = RetroPassOutputSize(pass, false, 256, 224, 1920, 1080);
    CHECK(size.width == 256);
    CHECK(size.height == 224);
    CHECK_FALSE(size.into_frame);
    size = RetroPassOutputSize(pass, true, 256, 224, 1920, 1080);
    CHECK(size.into_frame);
    CHECK(size.width == 1920);
    CHECK(size.height == 1080);
}

TEST_CASE("pass scales: source, viewport and absolute") {
    RetroPassSpec pass;
    pass.scale_given = true;
    pass.scale_x = RetroScale::Source;
    pass.factor_x = 2;
    pass.scale_y = RetroScale::Absolute;
    pass.factor_y = 100;
    RetroPassSize size = RetroPassOutputSize(pass, true, 256, 224, 1920, 1080);
    CHECK_FALSE(size.into_frame); // the last pass asked for its own size
    CHECK(size.width == 512);
    CHECK(size.height == 100);

    pass.scale_x = pass.scale_y = RetroScale::Viewport;
    pass.factor_x = pass.factor_y = 0.5f;
    size = RetroPassOutputSize(pass, true, 256, 224, 1920, 1080);
    CHECK_FALSE(size.into_frame);
    CHECK(size.width == 960);
    CHECK(size.height == 540);

    pass.factor_x = pass.factor_y = 1;
    CHECK(RetroPassOutputSize(pass, true, 256, 224, 1920, 1080).into_frame);
    CHECK_FALSE(RetroPassOutputSize(pass, false, 256, 224, 1920, 1080).into_frame);
}

TEST_CASE("textures are sampled the way the pass after their writer reads") {
    CHECK(RetroReaderPass(RetroSem::TexSource, 0, 3) == 3);
    CHECK(RetroReaderPass(RetroSem::TexPassOutput, 1, 3) == 2);
    CHECK(RetroReaderPass(RetroSem::TexPassFeedback, 4, 1) == 5);
    CHECK(RetroReaderPass(RetroSem::TexOriginal, 0, 3) == 0);
    CHECK(RetroReaderPass(RetroSem::TexHistory, 2, 3) == 0);
    CHECK(RetroReaderPass(RetroSem::TexLut, 0, 2) == 2);
}

TEST_CASE("RetroArch's uniform names resolve to their values") {
    RetroNames names;
    names.params = {"STRENGTH"};
    names.aliases = {"", "Blurred", ""};
    names.luts = {"Mask"};
    const auto is = [&](const char* name, int pass, RetroSem sem, int index) {
        const auto found = ResolveRetroName(name, pass, names);
        return found && found->first == sem && found->second == index;
    };
    CHECK(is("Source", 2, RetroSem::TexSource, 0));
    CHECK(is("SourceSize", 2, RetroSem::Source, 0));
    CHECK(is("Original", 2, RetroSem::TexOriginal, 0));
    CHECK(is("OutputSize", 2, RetroSem::Output, 0));
    CHECK(is("FrameCount", 0, RetroSem::FrameCount, 0));
    CHECK(is("PassOutput0", 2, RetroSem::TexPassOutput, 0));
    CHECK(is("PassFeedback1", 0, RetroSem::TexPassFeedback, 1));
    CHECK(is("Blurred", 2, RetroSem::TexPassOutput, 1));
    CHECK(is("BlurredSize", 2, RetroSem::PassOutputSize, 1));
    CHECK(is("Mask", 0, RetroSem::TexLut, 0));
    CHECK(is("STRENGTH", 0, RetroSem::Param, 0));
    CHECK(is("params.STRENGTH", 0, RetroSem::Param, 0));
    CHECK(is("MaskSize", 0, RetroSem::LutSize, 0));
    // The older GLSL names count passes back from this one and earlier pictures from one.
    CHECK(is("PassPrev2Texture", 3, RetroSem::TexPassOutput, 1));
    CHECK(is("PassPrev4Texture", 3, RetroSem::TexOriginal, 0));
    CHECK(is("Prev1Texture", 0, RetroSem::TexHistory, 2));
    CHECK(is("PrevTexture", 0, RetroSem::TexHistory, 1));
    CHECK(is("Pass1TextureSize", 2, RetroSem::PassOutputSize, 0));
    CHECK_FALSE(ResolveRetroName("SomethingElse", 0, names));
}
