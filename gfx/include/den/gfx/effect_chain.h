// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A small multi-pass post-processing chain: a picture goes through a list of fragment
// shaders, each drawing into a render target the next one reads, the last one into the
// frame. Written once for GL 3.3 and GLES 3 (Device::ShaderHeader).
//
// What every pass can use (declared for it, see effect_chain.cpp):
//
//     Source(p)    the pass before it; the picture itself for the first pass
//     Original(p)  the picture as it came in
//     History(p)   what this pass drew for the previous frame (feedback passes)
//     u_source_size, u_original_size, u_output_size   in pixels
//     u_frame      a frame counter, for effects that move
//     v_uv         0..1 over the picture, row 0 at the top
//     o_color      the result
//
// and one "uniform float p_<id>" per parameter of the effect. Colours between passes are
// stored as the picture's own (gamma) values in 8 bits.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "den/gfx/device.h"
#include "den/gfx/types.h"

namespace den::gfx {

struct EffectPass {
    std::string name;     // for the log
    std::string fragment; // GLSL after the declarations above: helpers and main()
    /// Size of the pass's target: a multiple of the picture's size, or of the size it is
    /// finally shown at. The last pass always draws at the shown size.
    enum class Size { Source, Output } size = Size::Source;
    float scale_x = 1, scale_y = 1;
    bool linear = true;    // how the next pass samples this one
    bool feedback = false; // keeps its previous frame for History()
};

struct EffectSpec {
    std::vector<EffectPass> passes;
    std::vector<std::string> params; // ids; each is the uniform "p_<id>"
};

class EffectChain {
public:
    /// The picture, or a part of it: `uv` places it in the texture (h may be negative).
    struct Input {
        unsigned texture = 0;
        Rect uv{0, 0, 1, 1};
        float width = 0, height = 0; // its own pixels
        std::uint64_t serial = 0;    // changes with every new picture
        std::uint64_t frame = 0;     // u_frame
    };
    /// Where the last pass draws: a rectangle (pixels, top-left origin) of a framebuffer.
    struct Output {
        unsigned framebuffer = 0;
        Rect target;
        int view_width = 0, view_height = 0; // the framebuffer's size
    };

    /// Compiles the passes; false (and nothing loaded) when one fails.
    bool Load(Device& device, const EffectSpec& spec);
    bool Loaded() const {
        return !passes_.empty();
    }
    /// A parameter by its position in EffectSpec::params.
    void SetParam(std::size_t index, float value);

    /// Runs the chain with its own GL state; the caller flushes its canvas before and
    /// restores it after. `region` keeps the targets (and the history) of one part of the
    /// picture apart from the others: a two-screen console draws each screen with its own.
    bool Draw(Device& device, const Input& input, const Output& output, int region = 0);

    /// Frees the GL objects; the context must be current.
    void Release();

private:
    struct Target {
        Texture texture;
        unsigned framebuffer = 0;
    };
    struct Pass {
        EffectPass spec;
        unsigned program = 0;
        int u_source = -1, u_original = -1, u_history = -1;
        int u_source_uv = -1, u_original_uv = -1;
        int u_source_size = -1, u_original_size = -1, u_output_size = -1;
        int u_frame = -1, u_flip = -1;
        std::vector<int> params;
    };
    struct PassTargets {
        Target now, before; // `before` only for feedback passes
    };
    struct Region {
        std::vector<PassTargets> targets;
        std::uint64_t serial = 0;
        bool drawn = false;
    };

    /// True when the target was made anew.
    bool Ensure(Device& device, Target& target, int width, int height, bool linear);
    void Free(Region& region);

    std::vector<Pass> passes_;
    std::vector<float> values_;
    std::vector<Region> regions_;
    unsigned vao_ = 0, vbo_ = 0;
};

} // namespace den::gfx
