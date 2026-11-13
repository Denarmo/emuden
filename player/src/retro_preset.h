// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Reading RetroArch shader presets (.slangp, .glslp) and their shaders into a
// gfx::RetroSpec: "key = value" lines, "#reference" to build on another preset, shader
// files with "#include", "#pragma parameter", "#pragma name" and, for Slang,
// "#pragma stage". Paths are relative to the file that names them.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "den/base/result.h"
#include "den/gfx/retro_chain.h"

namespace den::player {

/// True for a file this reads, by its extension.
bool IsRetroPreset(const std::filesystem::path& file);

/// The preset with its shaders' sources; the LUT images only when `images` (a listing of
/// presets wants the sliders, not the pictures).
Result<gfx::RetroSpec> LoadRetroPreset(const std::filesystem::path& file, bool images = true);

/// One shader file, its includes put in: the stages (Slang) or the file twice (GLSL), the
/// parameters it declares and the name it asks to be read by.
struct RetroShaderSource {
    std::string vertex, fragment;
    std::vector<gfx::RetroParam> params;
    std::string alias;
    std::string format; // "#pragma format", Slang only
};
Result<RetroShaderSource> LoadRetroShader(const std::filesystem::path& file, gfx::RetroLanguage language);

/// Parses one "#pragma parameter" line; false when it is not a well-formed one.
bool ParseRetroParam(std::string_view line, gfx::RetroParam& out);

} // namespace den::player
