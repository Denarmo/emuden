// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/gfx/retro_chain.h"

#include <algorithm>
#include <cmath>

#include "den/gfx/gl.h"
#include "retro_chain_state.h"
#include "retro_layout.h"

namespace den::gfx {

namespace retro {

void FreeImage(Image& image) {
    if (image.framebuffer) {
        gl::DeleteFramebuffers(1, &image.framebuffer);
    }
    if (image.id) {
        gl::DeleteTextures(1, &image.id);
    }
    image = Image{};
}

} // namespace retro

using namespace retro;

void RetroChain::State::Sample(unsigned texture, bool linear, RetroWrap wrap, bool mipmap) const {
    gl::BindTexture(gl::TEXTURE_2D, texture);
    if (!texture) {
        return;
    }
    if (mipmap) {
        gl::GenerateMipmap(gl::TEXTURE_2D);
    }
    const gl::Enum min = mipmap ? (linear ? gl::LINEAR_MIPMAP_LINEAR : gl::NEAREST_MIPMAP_NEAREST)
                                : (linear ? gl::LINEAR : gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<int>(min));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<int>(linear ? gl::LINEAR : gl::NEAREST));
    gl::Enum mode = gl::CLAMP_TO_EDGE;
    switch (wrap) {
    case RetroWrap::Border:
        // Transparent black beyond the edge; GL ES 3.0 has no border, the edge is nearest.
        mode = gles ? gl::CLAMP_TO_EDGE : gl::CLAMP_TO_BORDER;
        break;
    case RetroWrap::Edge:
        mode = gl::CLAMP_TO_EDGE;
        break;
    case RetroWrap::Repeat:
        mode = gl::REPEAT;
        break;
    case RetroWrap::Mirror:
        mode = gl::MIRRORED_REPEAT;
        break;
    }
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<int>(mode));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<int>(mode));
}

bool RetroChain::State::Ensure(Image& image, int width, int height, gl::Enum format) {
    width = std::clamp(width, 1, MAX_SIDE);
    height = std::clamp(height, 1, MAX_SIDE);
    if ((format == gl::RGBA16F || format == gl::RGBA32F) && !float_targets) {
        format = gl::RGBA8;
    }
    if (image.id && image.width == width && image.height == height && image.format == format) {
        return false;
    }
    FreeImage(image);
    gl::GenTextures(1, &image.id);
    gl::BindTexture(gl::TEXTURE_2D, image.id);
    const gl::Enum type = format == gl::RGBA32F   ? gl::FLOAT
                          : format == gl::RGBA16F ? gl::HALF_FLOAT
                                                  : gl::UNSIGNED_BYTE;
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<int>(format), width, height, 0, gl::RGBA, type, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<int>(gl::NEAREST));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<int>(gl::NEAREST));
    gl::GenFramebuffers(1, &image.framebuffer);
    gl::BindFramebuffer(gl::FRAMEBUFFER, image.framebuffer);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, image.id, 0);
    if (gl::CheckFramebufferStatus(gl::FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE && format != gl::RGBA8) {
        // A format this device cannot draw into: half floats, then plain 8 bits.
        FreeImage(image);
        return Ensure(image, width, height, format == gl::RGBA32F ? gl::RGBA16F : gl::RGBA8);
    }
    image.width = width;
    image.height = height;
    image.format = format;
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(gl::COLOR_BUFFER_BIT);
    return true;
}

void RetroChain::State::Release() {
    for (Region& region : regions) {
        FreeImage(region.original);
        for (Image& image : region.history) {
            FreeImage(image);
        }
        for (Image& image : region.outputs) {
            FreeImage(image);
        }
        for (Image& image : region.previous) {
            FreeImage(image);
        }
    }
    regions.clear();
    for (Image& lut : luts) {
        FreeImage(lut);
    }
    luts.clear();
    for (Pass& pass : passes) {
        if (pass.program) {
            gl::DeleteProgram(pass.program);
        }
    }
    passes.clear();
    if (copy) {
        gl::DeleteProgram(copy);
        copy = 0;
    }
    if (vbo) {
        gl::DeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao) {
        gl::DeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

RetroChain::RetroChain() = default;

RetroChain::~RetroChain() = default;

bool RetroChain::Loaded() const {
    return state_ && !state_->passes.empty();
}

void RetroChain::SetParam(std::size_t index, float value) {
    if (state_ && index < state_->values.size()) {
        state_->values[index] = value;
    }
}

void RetroChain::Release() {
    if (state_) {
        state_->Release();
        state_.reset();
    }
}

bool RetroChain::Draw(Device&, const EffectChain::Input& input, const EffectChain::Output& output, int region_index) {
    if (!Loaded() || region_index < 0 || !input.texture || input.width < 1 || input.height < 1 ||
        output.target.w < 1 || output.target.h < 1 || output.view_width <= 0 || output.view_height <= 0) {
        return false;
    }
    State& s = *state_;
    if (static_cast<std::size_t>(region_index) >= s.regions.size()) {
        s.regions.resize(static_cast<std::size_t>(region_index) + 1);
    }
    State::Region& region = s.regions[static_cast<std::size_t>(region_index)];
    region.outputs.resize(s.passes.size());
    region.previous.resize(s.passes.size());
    region.history.resize(static_cast<std::size_t>(s.history));
    const bool new_picture = !region.drawn || region.serial != input.serial;

    gl::Disable(gl::BLEND);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::BindVertexArray(s.vao);
    gl::VertexAttrib4f(2, 1, 1, 1, 1);

    const int width = static_cast<int>(std::lround(input.width));
    const int height = static_cast<int>(std::lround(input.height));
    if (new_picture) {
        // The picture before becomes the newest of the history, the oldest is drawn over.
        if (!region.history.empty() && region.original.id) {
            std::rotate(region.history.rbegin(), region.history.rbegin() + 1, region.history.rend());
            std::swap(region.history.front(), region.original);
        }
        for (std::size_t k = 0; k < s.passes.size(); ++k) {
            if (s.feedback[k]) {
                std::swap(region.outputs[k], region.previous[k]);
            }
        }
        ++region.frame_count;
    }
    s.Ensure(region.original, width, height, gl::RGBA8);
    if (new_picture) {
        gl::BindFramebuffer(gl::FRAMEBUFFER, region.original.framebuffer);
        gl::Viewport(0, 0, region.original.width, region.original.height);
        gl::UseProgram(s.copy);
        gl::ActiveTexture(gl::TEXTURE0);
        s.Sample(input.texture, false, RetroWrap::Edge, false);
        gl::Uniform1i(s.copy_texture, 0);
        gl::Uniform4f(s.copy_uv, input.uv.x, input.uv.y, input.uv.w, input.uv.h);
        gl::Uniform1f(s.copy_flip, 0.0f);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    }

    const float view_w = output.target.w, view_h = output.target.h;
    const auto size_of = [](const Image& image) { return std::pair<float, float>(float(image.width), float(image.height)); };
    std::pair<float, float> source = size_of(region.original);
    bool into_frame = false;
    for (std::size_t p = 0; p < s.passes.size(); ++p) {
        const State::Pass& pass = s.passes[p];
        const bool last = p + 1 == s.passes.size();
        const RetroPassSize size = RetroPassOutputSize(pass.spec, last, source.first, source.second, view_w, view_h);
        float out_w = size.width;
        float out_h = size.height;
        into_frame = size.into_frame;
        if (into_frame) {
            if (!s.gles) {
                gl::Disable(gl::FRAMEBUFFER_SRGB);
            }
            gl::BindFramebuffer(gl::FRAMEBUFFER, output.framebuffer);
            const int x = static_cast<int>(std::lround(output.target.x));
            const int y = static_cast<int>(std::lround(output.target.y));
            gl::Viewport(x, output.view_height - y - static_cast<int>(std::lround(view_h)),
                         static_cast<int>(std::lround(view_w)), static_cast<int>(std::lround(view_h)));
        } else {
            const gl::Enum format = pass.spec.float32       ? gl::RGBA32F
                                    : pass.spec.float_target  ? gl::RGBA16F
                                    : pass.spec.srgb_target ? gl::SRGB8_ALPHA8
                                                            : gl::RGBA8;
            Image& target = region.outputs[p];
            s.Ensure(target, static_cast<int>(std::lround(out_w)), static_cast<int>(std::lround(out_h)), format);
            gl::BindFramebuffer(gl::FRAMEBUFFER, target.framebuffer);
            gl::Viewport(0, 0, target.width, target.height);
            if (!s.gles) {
                // Desktop GL encodes into an sRGB target only when asked; GL ES always does.
                if (target.format == gl::SRGB8_ALPHA8) {
                    gl::Enable(gl::FRAMEBUFFER_SRGB);
                } else {
                    gl::Disable(gl::FRAMEBUFFER_SRGB);
                }
            }
            out_w = static_cast<float>(target.width);
            out_h = static_cast<float>(target.height);
        }

        gl::UseProgram(pass.program);
        // How a texture is sampled: by the pass that reads it as its source.
        const auto settings_of = [&s](std::size_t reader) -> const RetroPassSpec& {
            return s.passes[std::min(reader, s.passes.size() - 1)].spec;
        };
        const auto image_of = [&](Sem sem, int index) -> const Image* {
            const auto k = static_cast<std::size_t>(std::max(index, 0));
            switch (sem) {
            case Sem::TexSource:
                return p == 0 ? &region.original : &region.outputs[p - 1];
            case Sem::TexOriginal:
                return &region.original;
            case Sem::TexHistory:
                return index == 0 ? &region.original : (k - 1 < region.history.size() ? &region.history[k - 1] : nullptr);
            case Sem::TexPassOutput:
                return index >= 0 && k < p ? &region.outputs[k] : nullptr;
            case Sem::TexPassFeedback:
                return index >= 0 && k < region.previous.size() ? &region.previous[k] : nullptr;
            case Sem::TexLut:
                return index >= 0 && k < s.luts.size() ? &s.luts[k] : nullptr;
            default:
                return nullptr;
            }
        };
        for (const Binding& binding : pass.bindings) {
            const auto set_size = [&binding](float w, float h) {
                w = std::max(w, 1.0f);
                h = std::max(h, 1.0f);
                if (binding.type == gl::FLOAT_VEC4) {
                    gl::Uniform4f(binding.location, w, h, 1.0f / w, 1.0f / h);
                } else {
                    gl::Uniform2f(binding.location, w, h);
                }
            };
            const auto set_count = [&binding](unsigned value) {
                if (binding.type == gl::UNSIGNED_INT) {
                    gl::Uniform1ui(binding.location, value);
                } else if (binding.type == gl::INT) {
                    gl::Uniform1i(binding.location, static_cast<int>(value));
                } else {
                    gl::Uniform1f(binding.location, static_cast<float>(value));
                }
            };
            const auto k = static_cast<std::size_t>(std::max(binding.index, 0));
            switch (binding.sem) {
            case Sem::Mvp:
                gl::UniformMatrix4fv(binding.location, 1, gl::FALSE_, into_frame ? MVP_FRAME : MVP_TARGET);
                break;
            case Sem::Output:
                set_size(out_w, out_h);
                break;
            case Sem::FinalViewport:
                set_size(view_w, view_h);
                break;
            case Sem::Source:
                set_size(source.first, source.second);
                break;
            case Sem::Original:
                set_size(input.width, input.height);
                break;
            case Sem::FrameCount: {
                const unsigned mod = pass.spec.frame_count_mod;
                set_count(mod ? region.frame_count % mod : region.frame_count);
                break;
            }
            case Sem::FrameDirection:
                if (binding.type == gl::FLOAT) {
                    gl::Uniform1f(binding.location, 1.0f);
                } else {
                    gl::Uniform1i(binding.location, 1);
                }
                break;
            case Sem::PassOutputSize:
                if (k < region.outputs.size()) {
                    const auto [w, h] = size_of(region.outputs[k]);
                    set_size(w, h);
                }
                break;
            case Sem::PassFeedbackSize:
                if (k < region.previous.size()) {
                    const auto [w, h] = size_of(region.previous[k]);
                    set_size(w, h);
                }
                break;
            case Sem::HistorySize:
                set_size(input.width, input.height);
                break;
            case Sem::LutSize:
                if (k < s.luts.size()) {
                    const auto [w, h] = size_of(s.luts[k]);
                    set_size(w, h);
                }
                break;
            case Sem::Param:
                if (k < s.values.size()) {
                    gl::Uniform1f(binding.location, s.values[k]);
                }
                break;
            default: {
                gl::ActiveTexture(gl::TEXTURE0 + static_cast<unsigned>(binding.unit));
                const Image* image = image_of(binding.sem, binding.index);
                if (binding.sem == Sem::TexLut && k < s.lut_specs.size()) {
                    const RetroLut& lut = s.lut_specs[k];
                    s.Sample(image ? image->id : 0, lut.linear, lut.wrap, lut.mipmap);
                } else {
                    // Read the way the pass after the one that drew it reads it.
                    const RetroPassSpec& reads = settings_of(RetroReaderPass(binding.sem, binding.index, p));
                    s.Sample(image ? image->id : 0, reads.linear, reads.wrap, reads.mipmap_input);
                }
                break;
            }
            }
        }
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        source = {out_w, out_h};
    }

    if (!s.gles) {
        gl::Disable(gl::FRAMEBUFFER_SRGB);
    }
    if (!into_frame) {
        // The last pass asked for a size of its own: stretched over the frame's area.
        const Image& last = region.outputs.back();
        gl::BindFramebuffer(gl::FRAMEBUFFER, output.framebuffer);
        const int x = static_cast<int>(std::lround(output.target.x));
        const int y = static_cast<int>(std::lround(output.target.y));
        gl::Viewport(x, output.view_height - y - static_cast<int>(std::lround(view_h)),
                     static_cast<int>(std::lround(view_w)), static_cast<int>(std::lround(view_h)));
        gl::UseProgram(s.copy);
        gl::ActiveTexture(gl::TEXTURE0);
        s.Sample(last.id, s.passes.back().spec.linear, RetroWrap::Edge, false);
        gl::Uniform1i(s.copy_texture, 0);
        gl::Uniform4f(s.copy_uv, 0, 0, 1, 1);
        gl::Uniform1f(s.copy_flip, 1.0f);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    }
    region.serial = input.serial;
    region.drawn = true;

    for (int unit = 15; unit >= 0; --unit) {
        gl::ActiveTexture(gl::TEXTURE0 + static_cast<unsigned>(unit));
        gl::BindTexture(gl::TEXTURE_2D, 0);
    }
    gl::UseProgram(0);
    gl::BindVertexArray(0);
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    return true;
}

} // namespace den::gfx
