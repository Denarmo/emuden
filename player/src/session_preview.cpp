// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Previews of the picture presets: the latest picture through any preset, cropped to its
// middle at the size the game's display shows it.

#include <cmath>

#include "den/base/log.h"
#include "den/gfx/gl.h"
#include "den/platform/pal.h"
#include "retro_preset.h"
#include "session_impl.h"

namespace den::player {

namespace {

constexpr int FRAMES = 3; // drawn a few times, for the presets that blend in frames before

} // namespace

gfx::Texture PlayerSession::PicturePreview(gfx::Canvas& canvas, const std::string& id, int width, int height) {
    PictureSource source;
    if (!core_ || width < 1 || height < 1 || shown_w_ < 1 || shown_h_ < 1 || !PreparePicture(canvas, source)) {
        return {};
    }
    const PresetData* preset = FindPreset(id);
    gfx::Device& device = canvas.GetDevice();
    const double start = pal::NowSeconds();
    canvas.Flush();
    // The canvas may be clipping (a scrolled list): none of that applies here.
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::BLEND);

    gfx::Texture out = device.CreateTexture(width, height, gfx::PixelFormat::Rgba8, nullptr, true);
    unsigned framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(gl::FRAMEBUFFER, framebuffer);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, out.id, 0);
    gl::Viewport(0, 0, width, height);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT);

    const gfx::Rect& part = shown_part_;
    const gfx::Rect uv{source.uv.x + part.x * source.uv.w, source.uv.y + part.y * source.uv.h, part.w * source.uv.w,
                       part.h * source.uv.h};
    // The picture at its shown size, its middle on the preview's middle.
    const gfx::Rect target{std::round((static_cast<float>(width) - shown_w_) * 0.5f),
                           std::round((static_cast<float>(height) - shown_h_) * 0.5f), std::round(shown_w_),
                           std::round(shown_h_)};
    const bool chained = preset && (!preset->passes.empty() || !preset->retro.empty());
    const bool linear = chained ? preset->input_linear : smooth_;
    gl::BindTexture(gl::TEXTURE_2D, source.texture.id);
    const int filter = linear ? gl::LINEAR : gl::NEAREST;
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
    gl::BindTexture(gl::TEXTURE_2D, 0);
    texture_filter_ = -1; // the next frame sets the filter it wants again

    gfx::EffectChain::Input input;
    input.texture = source.texture.id;
    input.uv = uv;
    input.width = source.width * part.w;
    input.height = source.height * part.h;
    gfx::EffectChain::Output output;
    output.framebuffer = framebuffer;
    output.target = target;
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
        gfx::EffectSpec spec;
        gfx::EffectPass pass;
        pass.name = "preview/plain";
        pass.fragment = "void main() {\n    o_color = Source(v_uv);\n}\n";
        spec.passes.push_back(std::move(pass));
        gfx::EffectChain chain;
        if (chain.Load(device, spec)) {
            run(chain);
        }
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &framebuffer);
    canvas.RestoreState();
    log::Info("player", "picture preview {}: {:.0f} ms", id, (pal::NowSeconds() - start) * 1000.0);
    return out;
}

} // namespace den::player
