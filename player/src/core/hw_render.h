// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What a core that draws with the GPU (libretro hardware rendering, OpenGL) needs from the
// player: a context and a framebuffer to draw into. Every call comes from the thread that
// runs the core.

#pragma once

#include <cstdint>
#include <vector>

namespace den::player {

struct HwRenderRequest {
    enum class Api {
        OpenGL,     // compatibility profile, any version
        OpenGLCore, // core profile of at least major.minor
        OpenGLES,   // OpenGL ES of at least major.minor
    };
    Api api = Api::OpenGL;
    unsigned major = 0;
    unsigned minor = 0;
    bool depth = false;
    bool stencil = false;
    /// The core draws with row 0 at the bottom of the picture, as OpenGL does.
    bool bottom_left_origin = false;
};

class HwRenderHost {
public:
    virtual ~HwRenderHost() = default;

    virtual HwRenderRequest::Api PreferredApi() const = 0;
    /// Makes a context of this kind, replacing an earlier one. False when the host cannot
    /// give one; the core then asks for another kind or gives up.
    virtual bool Accept(const HwRenderRequest& request) = 0;
    /// Makes the core's context current; Release gives the caller's own back.
    virtual bool MakeCurrent() = 0;
    virtual void Release() = 0;
    virtual void* GetProcAddress(const char* name) = 0;
    /// The framebuffer the core draws into.
    virtual std::uintptr_t CurrentFramebuffer() = 0;
    /// Gives the framebuffer this size, with the depth and stencil the core asked for. The
    /// core's context is current and its bindings are kept.
    virtual bool ResizeTarget(unsigned width, unsigned height) = 0;
    /// The core finished a frame: its picture is the lower left width x height of the target.
    virtual void Present(unsigned width, unsigned height) = 0;
    /// The picture last presented as RGBA rows, top row first.
    virtual bool ReadPicture(unsigned* width, unsigned* height,
                             std::vector<std::uint8_t>* rgba) = 0;
    /// Deletes the framebuffer and the context. Nothing is called after it but Accept.
    virtual void Shutdown() = 0;
};

} // namespace den::player
