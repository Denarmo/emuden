// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Showing the core's picture inside the rectangle the UI gives.

#include <algorithm>
#include <cmath>

#include "den/gfx/gl.h"
#include "session_impl.h"

namespace den::player {

gfx::Rect PlayerSession::PictureRect(const gfx::Canvas& canvas, const gfx::Rect& area) const {
    const VideoFrame& frame = core_->Frame();
    float aspect = aspect_override_ > 0 ? aspect_override_ : av_.aspect_ratio;
    if (aspect <= 0 && frame.height > 0) {
        aspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
    }
    if (aspect <= 0 || area.w <= 0 || area.h <= 0) {
        return area;
    }
    float w = area.w, h = area.w / aspect;
    if (h > area.h) {
        h = area.h;
        w = area.h * aspect;
    }
    if (integer_scale_ && frame.height > 0) {
        // Whole multiples of the picture's height in device pixels.
        const float pixels = h * canvas.Scale();
        const float multiple = std::floor(pixels / static_cast<float>(frame.height));
        if (multiple >= 1.0f) {
            h = multiple * static_cast<float>(frame.height) / canvas.Scale();
            w = h * aspect;
        }
    }
    // On whole device pixels, so nearest filtering stays even.
    const float s = canvas.Scale();
    const float x = std::round((area.x + (area.w - w) * 0.5f) * s) / s;
    const float y = std::round((area.y + (area.h - h) * 0.5f) * s) / s;
    return gfx::Rect{x, y, std::round(w * s) / s, std::round(h * s) / s};
}

void PlayerSession::Draw(gfx::Canvas& canvas, const gfx::Rect& area) {
    canvas.FillRect(area, gfx::Color::Rgb(0x000000));
    if (!core_) {
        return;
    }
    const VideoFrame& frame = core_->Frame();
    if (frame.width == 0 || frame.height == 0) {
        return;
    }
    gfx::Device& device = canvas.GetDevice();
    gfx::Texture texture;
    gfx::Rect uv{0, 0, 1, 1};
    if (frame.hardware) {
        if (!hw_) {
            return;
        }
        texture = hw_->Target();
        if (!texture) {
            return;
        }
        const float u = static_cast<float>(hw_->PictureWidth()) / static_cast<float>(texture.width);
        const float v =
            static_cast<float>(hw_->PictureHeight()) / static_cast<float>(texture.height);
        // The picture is the lower left of the target; GL's row 0 is its bottom row.
        uv = hw_->BottomLeftOrigin() ? gfx::Rect{0, v, u, -v} : gfx::Rect{0, 0, u, v};
    } else {
        const int width = static_cast<int>(frame.width), height = static_cast<int>(frame.height);
        if (!texture_ || texture_.width != width || texture_.height != height) {
            canvas.Flush();
            if (texture_) {
                device.DestroyTexture(texture_);
            }
            texture_ = device.CreateTexture(width, height, gfx::PixelFormat::Rgba8,
                                            frame.rgba.data(), smooth_);
            uploaded_serial_ = frame.serial;
            canvas.RestoreState();
        } else if (uploaded_serial_ != frame.serial) {
            canvas.Flush();
            device.UpdateTexture(texture_, 0, 0, width, height, gfx::PixelFormat::Rgba8,
                                 frame.rgba.data());
            uploaded_serial_ = frame.serial;
            canvas.RestoreState();
        }
        texture = texture_;
    }
    picture_rect_ = PictureRect(canvas, area);

    // Pixel art at a whole multiple stays sharp; anything else is smoothed when asked.
    const bool whole = !frame.hardware && frame.height > 0 &&
                       std::fabs(std::fmod(picture_rect_.h * canvas.Scale(),
                                           static_cast<float>(frame.height))) < 0.5f;
    const int filter = smooth_ && !whole ? gl::LINEAR : gl::NEAREST;
    if (filter != texture_filter_ || texture.id != filtered_texture_) {
        canvas.Flush();
        gl::BindTexture(gl::TEXTURE_2D, texture.id);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
        gl::BindTexture(gl::TEXTURE_2D, 0);
        canvas.RestoreState();
        texture_filter_ = filter;
        filtered_texture_ = texture.id;
    }
    canvas.DrawImage(picture_rect_, texture, gfx::Color{255, 255, 255, 255}, 0, uv);
    // The texture changes before the canvas would draw it at the end of the frame.
    canvas.Flush();
}

} // namespace den::player
