// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What RetroChain keeps between frames, shared by retro_chain.cpp (drawing) and
// retro_chain_load.cpp (compiling a preset).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "den/gfx/gl.h"
#include "den/gfx/retro_chain.h"
#include "retro_names.h"

namespace den::gfx {

namespace retro {

inline constexpr int MAX_SIDE = 4096;
inline constexpr int MAX_HISTORY = 16;
inline constexpr std::size_t MAX_LOG = 800;

using Sem = RetroSem;

struct Binding {
    Sem sem = Sem::Mvp;
    int index = 0;
    int location = -1;
    gl::Enum type = 0;
    int unit = -1;
};

struct Image {
    unsigned id = 0, framebuffer = 0;
    int width = 0, height = 0;
    gl::Enum format = gl::RGBA8;
};

// The quad: position (x, y, 0, 1) and texture coordinate (u, v, 0, 0) per corner, with
// The quad: position (x, y, 0, 1) and texture coordinate (u, v, 0, 0) per corner, with
// v = 0 at y = 0, the top of every picture between passes.
inline constexpr float QUAD[] = {
    0, 0, 0, 1, 0, 0, 0, 0,
    1, 0, 0, 1, 1, 0, 0, 0,
    0, 1, 0, 1, 0, 1, 0, 0,
    1, 1, 0, 1, 1, 1, 0, 0,
};
// Column-major: into a target (row 0 at y = 0) and into the frame (row 0 at the top).
inline constexpr float MVP_TARGET[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
inline constexpr float MVP_FRAME[16] = {2, 0, 0, 0, 0, -2, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1};

void FreeImage(Image& image);

} // namespace retro

struct RetroChain::State {
    struct Pass {
        RetroPassSpec spec;
        unsigned program = 0;
        std::vector<retro::Binding> bindings;
    };
    struct Region {
        retro::Image original;
        std::vector<retro::Image> history; // the originals before, newest first
        std::vector<retro::Image> outputs, previous;
        std::uint64_t serial = 0;
        unsigned frame_count = 0;
        bool drawn = false;
    };

    bool gles = false;
    bool float_targets = false;
    std::vector<Pass> passes;
    std::vector<std::string> param_ids;
    std::vector<float> values;
    std::vector<RetroLut> lut_specs;
    std::vector<retro::Image> luts;
    std::vector<bool> feedback; // per pass: its previous frame is read
    int history = 0;            // earlier originals kept
    std::vector<Region> regions;
    unsigned vao = 0, vbo = 0;
    unsigned copy = 0;
    int copy_uv = -1, copy_flip = -1, copy_texture = -1;

    void Sample(unsigned texture, bool linear, RetroWrap wrap, bool mipmap) const;
    bool Ensure(retro::Image& image, int width, int height, gl::Enum format);
    void Release();
};

} // namespace den::gfx
