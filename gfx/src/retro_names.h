// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The names RetroArch gives a pass's uniforms and textures (retro_chain.cpp), in the Slang
// spelling and the older GLSL one.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace den::gfx {

// What a uniform or texture of a pass is, by RetroArch's names for it.
enum class RetroSem {
    Mvp, Output, FinalViewport, Source, Original, FrameCount, FrameDirection,
    PassOutputSize, PassFeedbackSize, HistorySize, LutSize, Param,
    TexSource, TexOriginal, TexPassOutput, TexPassFeedback, TexHistory, TexLut,
};

inline bool IsTexture(RetroSem sem) {
    return sem >= RetroSem::TexSource;
}

struct RetroNames {
    std::vector<std::string> params;  // the sliders' ids
    std::vector<std::string> aliases; // per pass, empty without one
    std::vector<std::string> luts;
};

/// What `name` (an active uniform, "[0]" and "block." allowed) means for pass `pass`, with
/// its index (a pass, an earlier picture, a slider, a texture); nothing for a name unknown.
std::optional<std::pair<RetroSem, int>> ResolveRetroName(std::string name, int pass, const RetroNames& names);

} // namespace den::gfx
