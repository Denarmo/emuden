// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The GL context a GPU-drawing core gets: its own context on the app's window, sharing
// objects with the app's context, and a framebuffer whose colour texture the app then draws.
// The core's GL state never mixes with the canvas'.

#pragma once

#include "core/hw_render.h"
#include "den/gfx/types.h"

namespace den::player {

class HwContext final : public HwRenderHost {
public:
    /// `gles`: the app's own context is OpenGL ES. Call on the thread that owns it, with it
    /// current.
    explicit HwContext(bool gles) : gles_(gles) {}
    ~HwContext() override;

    HwRenderRequest::Api PreferredApi() const override;
    bool Accept(const HwRenderRequest& request) override;
    bool MakeCurrent() override;
    void Release() override;
    void* GetProcAddress(const char* name) override;
    std::uintptr_t CurrentFramebuffer() override;
    bool ResizeTarget(unsigned width, unsigned height) override;
    void Present(unsigned width, unsigned height) override;
    bool ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba) override;
    void Shutdown() override;

    /// The target's colour texture (usable in the app's context) and the part of it the last
    /// frame fills.
    gfx::Texture Target() const {
        return gfx::Texture{texture_, static_cast<int>(width_), static_cast<int>(height_)};
    }
    unsigned PictureWidth() const {
        return picture_width_;
    }
    unsigned PictureHeight() const {
        return picture_height_;
    }
    bool BottomLeftOrigin() const {
        return request_.bottom_left_origin;
    }

private:
    bool gles_;
    HwRenderRequest request_;
    void* window_ = nullptr;  // SDL_Window
    void* main_ = nullptr;    // SDL_GLContext of the app
    void* context_ = nullptr; // SDL_GLContext of the core
    int current_depth_ = 0;
    unsigned fbo_ = 0, texture_ = 0, depth_ = 0;
    unsigned width_ = 0, height_ = 0;
    unsigned picture_width_ = 0, picture_height_ = 0;
};

} // namespace den::player
