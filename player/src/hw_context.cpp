// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include <SDL3/SDL_video.h>

#include "den/base/log.h"
#include "den/gfx/gl.h"
#include "hw_context.h"

namespace den::player {

namespace {

constexpr gl::Enum TEXTURE_BINDING_2D = 0x8069, RENDERBUFFER_BINDING = 0x8CA7;

SDL_Window* Window(void* window) {
    return static_cast<SDL_Window*>(window);
}

SDL_GLContext Context(void* context) {
    return static_cast<SDL_GLContext>(context);
}

} // namespace

HwContext::~HwContext() {
    Shutdown();
}

HwRenderRequest::Api HwContext::PreferredApi() const {
    return gles_ ? HwRenderRequest::Api::OpenGLES : HwRenderRequest::Api::OpenGLCore;
}

bool HwContext::Accept(const HwRenderRequest& request) {
    Shutdown();
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    SDL_GLContext main = SDL_GL_GetCurrentContext();
    if (!window || !main) {
        return false;
    }
    const bool wants_es = request.api == HwRenderRequest::Api::OpenGLES;
    if (gles_ && !wants_es) {
        return false; // an OpenGL ES device has no desktop OpenGL
    }
    int profile = SDL_GL_CONTEXT_PROFILE_CORE;
    int major = 3, minor = 3;
    if (wants_es) {
        profile = SDL_GL_CONTEXT_PROFILE_ES;
        major = std::max(3u, request.major);
        minor = request.major >= 3 ? static_cast<int>(request.minor) : 0;
    } else if (request.api == HwRenderRequest::Api::OpenGL) {
        profile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
        major = 2;
        minor = 1;
    } else if (request.major > 3 || (request.major == 3 && request.minor > 3)) {
        major = static_cast<int>(request.major);
        minor = static_cast<int>(request.minor);
    }
    // The attributes are global: put the app's own back once the context exists.
    int old_profile = 0, old_major = 0, old_minor = 0, old_share = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &old_profile);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &old_major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &old_minor);
    SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, &old_share);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, old_profile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, old_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, old_minor);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, old_share);
    SDL_GL_MakeCurrent(window, main);
    if (!context) {
        log::Warn("player", "no OpenGL context for the core ({} {}.{}): {}",
                  wants_es ? "ES" : "desktop", major, minor, SDL_GetError());
        return false;
    }
    window_ = window;
    main_ = main;
    context_ = context;
    request_ = request;
    return true;
}

bool HwContext::MakeCurrent() {
    if (!context_) {
        return false;
    }
    if (current_depth_++ == 0) {
        SDL_GL_MakeCurrent(Window(window_), Context(context_));
    }
    return true;
}

void HwContext::Release() {
    if (!context_ || current_depth_ == 0) {
        return;
    }
    if (--current_depth_ == 0) {
        // What the core drew must be in the shared texture before the app samples it.
        gl::Flush();
        SDL_GL_MakeCurrent(Window(window_), Context(main_));
    }
}

void* HwContext::GetProcAddress(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

std::uintptr_t HwContext::CurrentFramebuffer() {
    return fbo_;
}

bool HwContext::ResizeTarget(unsigned width, unsigned height) {
    if (!context_) {
        return false;
    }
    MakeCurrent();
    gl::Int old_fbo = 0, old_texture = 0, old_renderbuffer = 0;
    gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &old_fbo);
    gl::GetIntegerv(TEXTURE_BINDING_2D, &old_texture);
    gl::GetIntegerv(RENDERBUFFER_BINDING, &old_renderbuffer);

    if (fbo_ == 0) {
        gl::GenFramebuffers(1, &fbo_);
    }
    if (texture_ != 0) {
        gl::DeleteTextures(1, &texture_);
    }
    gl::GenTextures(1, &texture_);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    // No alpha: what a core leaves there must not make the picture transparent.
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGB8, width, height, 0, gl::RGB, gl::UNSIGNED_BYTE,
                   nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture_, 0);
    if (request_.depth || request_.stencil) {
        if (depth_ == 0) {
            gl::GenRenderbuffers(1, &depth_);
        }
        gl::BindRenderbuffer(gl::RENDERBUFFER, depth_);
        gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH24_STENCIL8, width, height);
        gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_STENCIL_ATTACHMENT,
                                    gl::RENDERBUFFER, depth_);
    }
    const bool complete = gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    if (complete) {
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, static_cast<gl::UInt>(old_fbo));
    gl::BindTexture(gl::TEXTURE_2D, static_cast<gl::UInt>(old_texture));
    gl::BindRenderbuffer(gl::RENDERBUFFER, static_cast<gl::UInt>(old_renderbuffer));
    Release();
    if (!complete) {
        log::Error("player", "the core's {}x{} framebuffer is incomplete", width, height);
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void HwContext::Present(unsigned width, unsigned height) {
    picture_width_ = std::min(width, width_);
    picture_height_ = std::min(height, height_);
}

bool HwContext::ReadPicture(unsigned* width, unsigned* height, std::vector<std::uint8_t>* rgba) {
    if (!context_ || texture_ == 0 || picture_width_ == 0 || picture_height_ == 0 ||
        current_depth_ != 0) {
        return false;
    }
    // In the app's context, through a framebuffer of its own: the core's state stays as it is.
    gl::Int old_fbo = 0;
    gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &old_fbo);
    gl::UInt fbo = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture_, 0);
    const bool complete = gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    const std::size_t row = static_cast<std::size_t>(picture_width_) * 4;
    std::vector<std::uint8_t> pixels(row * picture_height_);
    if (complete) {
        gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, picture_width_, picture_height_, gl::RGBA, gl::UNSIGNED_BYTE,
                       pixels.data());
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, static_cast<gl::UInt>(old_fbo));
    gl::DeleteFramebuffers(1, &fbo);
    if (!complete) {
        return false;
    }
    rgba->resize(pixels.size());
    for (unsigned y = 0; y < picture_height_; ++y) {
        const unsigned from = request_.bottom_left_origin ? picture_height_ - 1 - y : y;
        std::memcpy(rgba->data() + row * y, pixels.data() + row * from, row);
    }
    for (std::size_t i = 3; i < rgba->size(); i += 4) {
        (*rgba)[i] = 255;
    }
    *width = picture_width_;
    *height = picture_height_;
    return true;
}

void HwContext::Shutdown() {
    if (!context_) {
        return;
    }
    current_depth_ = 0;
    SDL_GL_MakeCurrent(Window(window_), Context(context_));
    if (fbo_ != 0) {
        gl::DeleteFramebuffers(1, &fbo_);
    }
    if (texture_ != 0) {
        gl::DeleteTextures(1, &texture_);
    }
    if (depth_ != 0) {
        gl::DeleteRenderbuffers(1, &depth_);
    }
    SDL_GL_MakeCurrent(Window(window_), Context(main_));
    SDL_GL_DestroyContext(Context(context_));
    fbo_ = texture_ = depth_ = 0;
    width_ = height_ = picture_width_ = picture_height_ = 0;
    context_ = nullptr;
}

} // namespace den::player
