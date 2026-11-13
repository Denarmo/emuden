// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Showing the core's picture inside the rectangle the UI gives. The path has three steps:
// PreparePicture makes the core's latest frame a texture (a PictureSource), Draw decides
// where it goes, and DrawRegion sends a part of it through the picture preset's chain to
// its place. Draw uses one region for the whole picture; DrawScreen is one screen of a
// console with two, with its own part, target and slot.

#include <algorithm>
#include <cmath>

#include "den/gfx/gl.h"
#include "picture_fit.h"
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
    // In device pixels: a whole multiple of the picture's rows when the user or the preset
    // asks for it, or when the preset draws a cell per pixel and it costs little.
    const float s = canvas.Scale();
    WholeScale whole = WholeScale::Off;
    if (integer_scale_ || (preset_ && preset_->integer)) {
        whole = WholeScale::Always;
    } else if (preset_ && preset_->near_integer) {
        whole = WholeScale::Near;
    }
    const PictureSize fit = FitPicture(area.w * s, area.h * s, aspect, static_cast<int>(frame.height), whole);
    const float w = fit.w / s, h = fit.h / s;
    // On whole device pixels, so nearest filtering stays even.
    const float x = std::round((area.x + (area.w - w) * 0.5f) * s) / s;
    const float y = std::round((area.y + (area.h - h) * 0.5f) * s) / s;
    return gfx::Rect{x, y, std::round(w * s) / s, std::round(h * s) / s};
}

bool PlayerSession::PreparePicture(gfx::Canvas& canvas, PictureSource& source) {
    const VideoFrame& frame = core_->Frame();
    if (frame.width == 0 || frame.height == 0) {
        return false;
    }
    gfx::Device& device = canvas.GetDevice();
    source.serial = frame.serial;
    source.hardware = frame.hardware;
    if (frame.hardware) {
        if (!hw_ || !hw_->Target()) {
            return false;
        }
        source.texture = hw_->Target();
        source.width = static_cast<float>(hw_->PictureWidth());
        source.height = static_cast<float>(hw_->PictureHeight());
        const float u = source.width / static_cast<float>(source.texture.width);
        const float v = source.height / static_cast<float>(source.texture.height);
        // The picture is the lower left of the target; GL's row 0 is its bottom row.
        source.uv = hw_->BottomLeftOrigin() ? gfx::Rect{0, v, u, -v} : gfx::Rect{0, 0, u, v};
        return true;
    }
    const int width = static_cast<int>(frame.width), height = static_cast<int>(frame.height);
    if (!texture_ || texture_.width != width || texture_.height != height) {
        canvas.Flush();
        if (texture_) {
            device.DestroyTexture(texture_);
        }
        texture_ = device.CreateTexture(width, height, gfx::PixelFormat::Rgba8, frame.rgba.data(),
                                        smooth_);
        texture_filter_ = -1;
        uploaded_serial_ = frame.serial;
        canvas.RestoreState();
    } else if (uploaded_serial_ != frame.serial) {
        canvas.Flush();
        device.UpdateTexture(texture_, 0, 0, width, height, gfx::PixelFormat::Rgba8, frame.rgba.data());
        uploaded_serial_ = frame.serial;
        canvas.RestoreState();
    }
    source.texture = texture_;
    source.width = static_cast<float>(width);
    source.height = static_cast<float>(height);
    source.uv = gfx::Rect{0, 0, 1, 1};
    return true;
}

void PlayerSession::DrawRegion(gfx::Canvas& canvas, const PictureSource& source,
                               const PictureRegion& region, int slot) {
    gfx::Device& device = canvas.GetDevice();
    const float s = canvas.Scale();
    const gfx::Rect uv{source.uv.x + region.part.x * source.uv.w, source.uv.y + region.part.y * source.uv.h,
                       region.part.w * source.uv.w, region.part.h * source.uv.h};
    const float rows = source.height * region.part.h;
    shown_part_ = region.part;
    shown_w_ = region.target.w * s;
    shown_h_ = region.target.h * s;
    if (chain_stale_) {
        // Compiling the preset's passes uses GL behind the canvas's back.
        canvas.Flush();
        ReadyChain(device);
        canvas.RestoreState();
    }
    const bool chained = ReadyChain(device);

    // Pixel art at a whole multiple stays sharp; anything else is smoothed when asked. A
    // preset says itself how its first pass reads the picture.
    const bool whole = !source.hardware && rows > 0 &&
                       std::fabs(std::fmod(region.target.h * s, rows)) < 0.5f;
    const bool linear = preset_ ? preset_->input_linear : smooth_ && !whole;
    const int filter = linear ? gl::LINEAR : gl::NEAREST;
    if (filter != texture_filter_ || source.texture.id != filtered_texture_) {
        canvas.Flush();
        gl::BindTexture(gl::TEXTURE_2D, source.texture.id);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
        gl::BindTexture(gl::TEXTURE_2D, 0);
        canvas.RestoreState();
        texture_filter_ = filter;
        filtered_texture_ = source.texture.id;
    }
    if (chained) {
        gfx::EffectChain::Input input;
        input.texture = source.texture.id;
        input.uv = uv;
        input.width = source.width * region.part.w;
        input.height = rows;
        input.serial = source.serial;
        input.frame = picture_frame_;
        gfx::EffectChain::Output output;
        output.target = gfx::Rect{region.target.x * s, region.target.y * s, region.target.w * s,
                                  region.target.h * s};
        output.view_width = static_cast<int>(std::lround(canvas.Width() * s));
        output.view_height = static_cast<int>(std::lround(canvas.Height() * s));
        canvas.Flush();
        const bool drawn = retro_.Loaded() ? retro_.Draw(device, input, output, slot)
                                           : chain_.Draw(device, input, output, slot);
        canvas.RestoreState();
        if (drawn) {
            return;
        }
    }
    canvas.DrawImage(region.target, source.texture, gfx::Color{255, 255, 255, 255}, 0, uv);
    // The texture changes before the canvas would draw it at the end of the frame.
    canvas.Flush();
}

void PlayerSession::Draw(gfx::Canvas& canvas, const gfx::Rect& area) {
    canvas.FillRect(area, gfx::Color::Rgb(0x000000));
    PictureSource source;
    if (!core_ || !PreparePicture(canvas, source)) {
        return;
    }
    picture_rect_ = PictureRect(canvas, area);
    ++picture_frame_;
    DrawRegion(canvas, source, PictureRegion{gfx::Rect{0, 0, 1, 1}, picture_rect_}, 0);
}

std::vector<GameScreen> PlayerSession::Screens() const {
    if (!core_) {
        return {};
    }
    const VideoFrame& frame = core_->Frame();
    return ScreensOf(system_, static_cast<int>(frame.width), static_cast<int>(frame.height));
}

void PlayerSession::DrawScreen(gfx::Canvas& canvas, int screen, const gfx::Rect& target) {
    PictureSource source;
    if (!core_ || target.w <= 0 || target.h <= 0 || !PreparePicture(canvas, source)) {
        return;
    }
    const std::vector<GameScreen> screens = Screens();
    if (screen < 0 || screen >= static_cast<int>(screens.size())) {
        return;
    }
    // The frame counter moves once per frame, whichever screens are shown.
    if (frame_owner_ == screen || frame_owner_ < 0 || frame_other_seen_) {
        ++picture_frame_;
        frame_owner_ = screen;
        frame_other_seen_ = false;
    } else {
        frame_other_seen_ = true;
    }
    // Slot 0 is the whole picture's (Draw); each screen keeps its own targets and history.
    DrawRegion(canvas, source, PictureRegion{screens[static_cast<std::size_t>(screen)].part, target},
               screen + 1);
}

} // namespace den::player
