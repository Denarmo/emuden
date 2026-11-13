// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Previews of the picture presets: one frame of the game, kept when the first preview is
// asked for (and again after CapturePreviewFrame), through any preset, cropped to its middle
// at the size the game's display shows it.

#include <cmath>

#include "den/base/log.h"
#include "den/gfx/gl.h"
#include "den/platform/pal.h"
#include "retro_preset.h"
#include "session_impl.h"

namespace den::player {

namespace {

constexpr int FRAMES = 3; // drawn a few times, for the presets that blend in frames before

gfx::EffectSpec PlainSpec() {
    gfx::EffectSpec spec;
    gfx::EffectPass pass;
    pass.name = "preview/plain";
    pass.fragment = "void main() {\n    o_color = Source(v_uv);\n}\n";
    pass.linear = false;
    spec.passes.push_back(std::move(pass));
    return spec;
}

struct Target {
    unsigned framebuffer = 0;
    explicit Target(const gfx::Texture& texture) {
        gl::GenFramebuffers(1, &framebuffer);
        gl::BindFramebuffer(gl::FRAMEBUFFER, framebuffer);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture.id, 0);
        gl::Viewport(0, 0, texture.width, texture.height);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
    }
    ~Target() {
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        gl::DeleteFramebuffers(1, &framebuffer);
    }
};

void SetFilter(unsigned texture, bool linear) {
    gl::BindTexture(gl::TEXTURE_2D, texture);
    const int filter = linear ? gl::LINEAR : gl::NEAREST;
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
    gl::BindTexture(gl::TEXTURE_2D, 0);
}

} // namespace

void PlayerSession::CapturePreviewFrame() {
    preview_stale_ = true;
}

void PlayerSession::ReleasePreviewFrame(gfx::Device& device) {
    if (preview_frame_) {
        device.DestroyTexture(preview_frame_);
    }
    preview_stale_ = true;
}

// The part of the latest picture DrawRegion showed last, at its own resolution; its first
// row is the picture's last, as a framebuffer has it.
bool PlayerSession::KeepPreviewFrame(gfx::Canvas& canvas) {
    PictureSource source;
    if (shown_w_ < 1 || shown_h_ < 1 || !PreparePicture(canvas, source)) {
        return false;
    }
    gfx::Device& device = canvas.GetDevice();
    const gfx::Rect& part = shown_part_;
    const int width = std::max(1, static_cast<int>(std::lround(source.width * part.w)));
    const int height = std::max(1, static_cast<int>(std::lround(source.height * part.h)));
    if (preview_frame_ && (preview_frame_.width != width || preview_frame_.height != height)) {
        device.DestroyTexture(preview_frame_);
    }
    if (!preview_frame_) {
        preview_frame_ = device.CreateTexture(width, height, gfx::PixelFormat::Rgba8, nullptr, false);
    }
    SetFilter(source.texture.id, false);
    texture_filter_ = -1; // the next frame sets the filter it wants again
    gfx::EffectChain chain;
    if (!chain.Load(device, PlainSpec())) {
        return false;
    }
    const Target target(preview_frame_);
    gfx::EffectChain::Input input;
    input.texture = source.texture.id;
    input.uv = gfx::Rect{source.uv.x + part.x * source.uv.w, source.uv.y + part.y * source.uv.h,
                         part.w * source.uv.w, part.h * source.uv.h};
    input.width = static_cast<float>(width);
    input.height = static_cast<float>(height);
    input.serial = input.frame = 1;
    gfx::EffectChain::Output output;
    output.framebuffer = target.framebuffer;
    output.target = gfx::Rect{0, 0, static_cast<float>(width), static_cast<float>(height)};
    output.view_width = width;
    output.view_height = height;
    const bool drawn = chain.Draw(device, input, output);
    chain.Release();
    preview_w_ = shown_w_;
    preview_h_ = shown_h_;
    preview_stale_ = !drawn;
    return drawn;
}

gfx::Texture PlayerSession::PicturePreview(gfx::Canvas& canvas, const std::string& id, int width, int height) {
    if (!core_ || width < 1 || height < 1) {
        return {};
    }
    gfx::Device& device = canvas.GetDevice();
    const double start = pal::NowSeconds();
    canvas.Flush();
    // The canvas may be clipping (a scrolled list): none of that applies here.
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::BLEND);
    if ((preview_stale_ || !preview_frame_) && !KeepPreviewFrame(canvas)) {
        canvas.RestoreState();
        return {};
    }

    const PresetData* preset = FindPreset(id);
    const bool chained = preset && (!preset->passes.empty() || !preset->retro.empty());
    SetFilter(preview_frame_.id, chained ? preset->input_linear : smooth_);
    gfx::Texture out = device.CreateTexture(width, height, gfx::PixelFormat::Rgba8, nullptr, true);
    const Target target(out);

    gfx::EffectChain::Input input;
    input.texture = preview_frame_.id;
    input.uv = gfx::Rect{0, 1, 1, -1};
    input.width = static_cast<float>(preview_frame_.width);
    input.height = static_cast<float>(preview_frame_.height);
    gfx::EffectChain::Output output;
    output.framebuffer = target.framebuffer;
    // The picture at its shown size, its middle on the preview's middle.
    output.target = gfx::Rect{std::round((static_cast<float>(width) - preview_w_) * 0.5f),
                              std::round((static_cast<float>(height) - preview_h_) * 0.5f), std::round(preview_w_),
                              std::round(preview_h_)};
    output.view_width = width;
    output.view_height = height;
    const std::vector<float> values = preset ? SavedValues(*preset) : std::vector<float>{};
    bool drawn = false;
    const auto run = [&](auto& chain) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            chain.SetParam(i, values[i]);
        }
        for (int frame = 1; frame <= FRAMES; ++frame) {
            input.serial = input.frame = static_cast<std::uint64_t>(frame);
            drawn = chain.Draw(device, input, output);
        }
        chain.Release();
    };
    if (chained && !preset->retro.empty()) {
        gfx::RetroChain chain;
        const auto spec = LoadRetroPreset(preset->retro);
        if (spec && chain.Load(device, spec.value())) {
            run(chain);
        }
    } else if (chained) {
        gfx::EffectChain chain;
        const auto spec = BuildEffect(*preset, ShaderDir());
        if (spec && chain.Load(device, spec.value())) {
            run(chain);
        }
    }
    if (!drawn) {
        // Plain, or a preset this device cannot run: the picture as it is.
        gfx::EffectChain chain;
        if (chain.Load(device, PlainSpec())) {
            run(chain);
        }
    }
    canvas.RestoreState();
    log::Info("player", "picture preview {}: {:.0f} ms", id, (pal::NowSeconds() - start) * 1000.0);
    return out;
}

} // namespace den::player
