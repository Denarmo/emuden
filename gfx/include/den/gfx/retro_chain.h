// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shaders in RetroArch's formats, as users import them: a preset (.slangp or .glslp) is a
// list of passes, each a shader with its own target size, filtering and wrapping, that can
// read the picture, every earlier pass, its own previous frame, earlier pictures and image
// textures (LUTs), with sliders declared in the shaders by "#pragma parameter".
//
// GLSL shaders (the older format) are written for GL and compile as they are, with VERTEX
// or FRAGMENT defined. Slang shaders are Vulkan GLSL: glslang turns each stage into SPIR-V
// and SPIRV-Cross back into the GLSL of this device, and the reflection tells which of
// RetroArch's values each uniform and texture is.
//
// Orientation: the first row of every texture is the top of its picture, as in Vulkan, so
// texture coordinate 0 is the top in both formats; the chain flips only into the frame.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/gfx/device.h"
#include "den/gfx/effect_chain.h"
#include "den/gfx/images.h"

namespace den::gfx {

enum class RetroLanguage { Glsl, Slang };
enum class RetroScale { Source, Viewport, Absolute };
enum class RetroWrap { Border, Edge, Repeat, Mirror };

/// A slider: "#pragma parameter ID "Description" initial minimum maximum [step]".
struct RetroParam {
    std::string id;
    std::string description;
    float initial = 0, minimum = 0, maximum = 1, step = 0.01f;
};

struct RetroPassSpec {
    std::string name;     // the shader's file, for the log
    std::string vertex;   // Slang: the vertex stage; GLSL: the whole file
    std::string fragment; // Slang: the fragment stage; GLSL: the whole file
    std::string alias;    // other passes read it by this name
    bool linear = false;  // how the next passes sample its output
    RetroWrap wrap = RetroWrap::Border;
    bool mipmap_input = false;
    bool float_target = false;
    bool float32 = false; // a float target at full precision
    bool srgb_target = false;
    bool scale_given = false; // without it a pass is the source's size, the last the viewport's
    RetroScale scale_x = RetroScale::Source, scale_y = RetroScale::Source;
    float factor_x = 1, factor_y = 1;
    unsigned frame_count_mod = 0;
};

struct RetroLut {
    std::string name;
    ImageData image;
    bool linear = false;
    RetroWrap wrap = RetroWrap::Border;
    bool mipmap = false;
};

struct RetroSpec {
    RetroLanguage language = RetroLanguage::Slang;
    std::vector<RetroPassSpec> passes;
    std::vector<RetroLut> luts;
    std::vector<RetroParam> params; // every pass's, the first declaration of an id winning
    std::vector<float> values;      // the preset's starting value for each of params
};

/// Runs a RetroArch preset over the picture, with the same inputs and outputs as
/// EffectChain. Main thread, with the GL context current.
class RetroChain {
public:
    RetroChain();
    ~RetroChain();
    RetroChain(const RetroChain&) = delete;
    RetroChain& operator=(const RetroChain&) = delete;

    /// Compiles every pass; on failure nothing is loaded and the status says which pass.
    Status Load(Device& device, const RetroSpec& spec);
    bool Loaded() const;
    void SetParam(std::size_t index, float value);
    bool Draw(Device& device, const EffectChain::Input& input, const EffectChain::Output& output,
              int region = 0);
    void Release();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace den::gfx
