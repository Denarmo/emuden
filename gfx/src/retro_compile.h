// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sources of RetroArch shaders made ready for this device's GL (retro_chain.h).

#pragma once

#include <string>

#include "den/base/result.h"

namespace den::gfx {

struct GlslStages {
    std::string vertex, fragment;
};

/// An older GLSL shader (both stages in one file): its own #version lines out, this
/// device's header in, and VERTEX or FRAGMENT defined for each stage. On GL ES a shader
/// without "#version 130" or later is GLSL ES 1.00, as RetroArch has it: such shaders use
/// "varying" and "texture2D", which GLSL ES 3.00 refuses.
GlslStages PrepareRetroGlsl(const std::string& source, std::string header, bool gles);

/// A Slang pass's two stages (Vulkan GLSL 450) as GLSL 3.00 ES or 3.30. Uniform blocks and
/// push constants become plain uniform structs, read by "<instance>.<member>"; what passes
/// from one stage to the other is named by its location, so the two stages always meet.
Result<GlslStages> CompileSlang(const std::string& vertex, const std::string& fragment, bool gles);

/// Compiles and links a program with the context current; 0 and `log` on failure. The older
/// GLSL attributes are bound to 0 (VertexCoord), 1 (TexCoord) and 2 (COLOR).
unsigned LinkRetroProgram(const std::string& vertex, const std::string& fragment, std::string& log);

} // namespace den::gfx
