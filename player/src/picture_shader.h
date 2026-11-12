// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The built-in picture presets: one GLSL pass that draws the game's picture as a CRT or an
// LCD would show it. Written for GL 3.3 and GLES 3 alike (Device::ShaderHeader).

#pragma once

#include <string>

#include "den/gfx/device.h"
#include "den/gfx/types.h"

namespace den::player {

class PictureShader {
public:
    struct Quad {
        unsigned texture = 0;
        gfx::Rect target;         // device pixels, top-left origin
        gfx::Rect uv;             // the picture inside the texture; h may be negative
        float source_width = 0;   // the picture's own pixels
        float source_height = 0;
        int view_width = 0;       // the framebuffer
        int view_height = 0;
    };

    /// True for a preset this class draws ("crt", "lcd"); "plain" is the canvas's business.
    static bool Has(const std::string& preset);

    /// Draws with its own GL state; the caller flushes the canvas before and restores it
    /// after. False when the preset has no pass or its shader failed: draw plainly then.
    bool Draw(gfx::Device& device, const std::string& preset, const Quad& quad);
    /// Frees the GL objects; the context must be current.
    void Release();

private:
    struct Pass {
        unsigned program = 0;
        int u_tex = -1, u_source = -1, u_output = -1, u_uv = -1;
        bool failed = false;
    };
    Pass* Find(gfx::Device& device, const std::string& preset);

    Pass crt_, lcd_;
    unsigned vao_ = 0, vbo_ = 0;
};

} // namespace den::player
