// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The picture presets as the player reads them from <assets>/shaders/presets.json, and the
// pieces the presentation path is built from: a source (the core's picture as a texture)
// and regions of it, each drawn through the preset's chain to its own place.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "den/base/result.h"
#include "den/gfx/effect_chain.h"
#include "den/player/session.h"

namespace den::player {

struct PresetPass {
    std::string shader;               // <shader>.glsl
    std::vector<std::string> defines; // "#define X 1" lines before it
    bool by_output = false;           // target sized from the shown size, not the picture's
    float scale_x = 1, scale_y = 1;
    bool linear = true;
    bool feedback = false;
};

struct PresetData {
    PicturePreset info;
    bool integer = false;     // shown at whole multiples of the picture's height
    bool near_integer = false; // the same, when the multiple is close to filling the area
    bool input_linear = true; // how the first pass samples the picture
    std::vector<PresetPass> passes;
    /// Every p_<id> uniform with the preset's value: the sliders first, in the order of
    /// info.params, then the fixed ones.
    std::vector<std::pair<std::string, float>> uniforms;
};

struct Catalogue {
    std::vector<PresetData> presets; // "plain" first
    std::vector<std::pair<std::string, std::string>> automatic; // system id, preset id
};

/// Reads presets.json of a shader folder. A preset without an id or with a pass whose file
/// is missing is left out; with no file at all only "plain" is left.
Catalogue LoadCatalogue(const std::filesystem::path& shader_dir);
/// The app's catalogue (<assets>/shaders), read once.
const Catalogue& Presets();
const PresetData* FindPreset(const std::string& id);
/// The chain of a preset, with the GLSL of its passes read from the shader folder.
Result<gfx::EffectSpec> BuildEffect(const PresetData& preset, const std::filesystem::path& shader_dir);
std::filesystem::path ShaderDir();

/// The core's latest picture as a texture. `uv` places the picture in the texture (h is
/// negative when the texture's first row is the picture's last).
struct PictureSource {
    gfx::Texture texture;
    gfx::Rect uv{0, 0, 1, 1};
    float width = 0, height = 0; // the picture's own pixels
    std::uint64_t serial = 0;
    bool hardware = false;
};

/// A part of the picture and where it goes. One region is the whole picture; a console
/// with two screens is two regions, each with its own `slot` so the chain keeps their
/// targets and histories apart (PlayerSession::DrawRegion).
struct PictureRegion {
    gfx::Rect part{0, 0, 1, 1}; // of the picture, 0..1, row 0 at the top
    gfx::Rect target;           // on the canvas, in its units
};

} // namespace den::player
