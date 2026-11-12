// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/gfx/gl.h"
#include "den/gfx/types.h"

namespace den::gfx {

enum class PixelFormat { Rgba8, R8 };

/// The GL device: one per context, used on the thread that owns the context.
class Device {
public:
    /// `gles` selects OpenGL ES 3 shader headers.
    Status Init(gl::ProcLoader loader, bool gles);
    bool Gles() const {
        return gles_;
    }
    /// The "#version" line and precision defaults every shader starts with.
    std::string ShaderHeader() const;
    /// Compiles and links; 0 and a logged error on failure. Attribute names are bound to
    /// locations 0..n in the order given.
    unsigned int CreateProgram(const std::string& vertex, const std::string& fragment,
                               const std::vector<const char*>& attributes);

    Texture CreateTexture(int width, int height, PixelFormat format, const void* pixels,
                          bool linear = true);
    void UpdateTexture(const Texture& texture, int x, int y, int width, int height,
                       PixelFormat format, const void* pixels);
    void DestroyTexture(Texture& texture);

    void BeginFrame(int width, int height, Color clear);
    /// The back buffer as tightly packed RGBA rows, top row first.
    std::vector<std::uint8_t> ReadPixels(int width, int height);

private:
    bool gles_ = false;
};

/// Writes RGBA pixels as a PNG.
Status SavePng(const std::filesystem::path& file, int width, int height, const std::uint8_t* rgba);

} // namespace den::gfx
