// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>
#include <vector>

#include "den/gfx/device.h"
#include "den/gfx/text.h"
#include "den/gfx/types.h"

namespace den::gfx {

/// The 2D batcher. Everything is given in logical units (pixels divided by the scale passed to
/// Begin), top-left origin. Shapes are anti-aliased rounded rectangles drawn by one shader, so
/// rectangles and text share a batch and only images break it.
class Canvas {
public:
    Status Init(Device& device, Text& text);

    void Begin(int pixel_width, int pixel_height, float scale);
    void End();
    float Width() const {
        return width_ / scale_;
    }
    float Height() const {
        return height_ / scale_;
    }
    float Scale() const {
        return scale_;
    }
    Text& GetText() {
        return *text_;
    }
    Device& GetDevice() {
        return *device_;
    }

    void FillRect(const Rect& rect, Color color, float radius = 0);
    void StrokeRect(const Rect& rect, Color color, float thickness, float radius = 0);
    /// `uv` is the part of the texture shown (x, y, w, h in 0..1).
    void DrawImage(const Rect& rect, const Texture& texture, Color tint = Color{255, 255, 255, 255},
                   float radius = 0, const Rect& uv = Rect{0, 0, 1, 1});
    /// One line of text with its top-left corner at (x, y). Returns the width drawn.
    float DrawText(std::string_view text, float x, float y, float size, Color color,
                   int font = FONT_REGULAR);

    void PushClip(const Rect& rect);
    void PopClip();
    Rect CurrentClip() const;

    /// Sends what is batched to the GPU. Code that issues its own GL calls (the player) calls
    /// Flush before and RestoreState after.
    void Flush();
    void RestoreState();

private:
    struct Vertex {
        float x, y, u, v;
        float lx, ly, hw, hh;
        float radius, border, mode;
        float r, g, b, a;
    };
    void Quad(const Rect& rect, const Rect& uv, Color color, float radius, float border, float mode,
              unsigned int texture);
    void ApplyClip();

    Device* device_ = nullptr;
    Text* text_ = nullptr;
    unsigned int program_ = 0, vao_ = 0, vbo_ = 0;
    int u_view_ = -1, u_tex_ = -1;
    int width_ = 0, height_ = 0;
    float scale_ = 1.0f;
    unsigned int texture_ = 0;
    std::vector<Vertex> vertices_;
    std::vector<Rect> clips_;
};

/// The part of a `tex_w` x `tex_h` image that fills `rect` without distortion (centre crop).
Rect CoverUv(int tex_w, int tex_h, const Rect& rect);
/// `rect` shrunk to the image's aspect ratio and centred (letterbox).
Rect FitRect(int tex_w, int tex_h, const Rect& rect);

} // namespace den::gfx
