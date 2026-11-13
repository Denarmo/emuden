// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "den/base/log.h"
#include "den/gfx/effect_chain.h"
#include "den/gfx/gl.h"

namespace den::gfx {

namespace {

constexpr int MAX_TARGET = 4096;
constexpr std::uint64_t FRAME_WRAP = 1u << 20; // u_frame stays exact as a float

// One quad over the whole viewport. u_flip puts row 0 of the picture at the top of the
// frame; without it row 0 is the first row of a target's texture, so every texture between
// passes reads the same way as the picture.
constexpr const char* VERTEX = R"(
in vec2 a_pos;
uniform float u_flip;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    v_uv.y = mix(v_uv.y, 1.0 - v_uv.y, u_flip);
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

constexpr const char* DECLARATIONS = R"(
precision highp int;
uniform sampler2D u_source;
uniform sampler2D u_original;
uniform sampler2D u_history;
uniform vec4 u_source_uv;
uniform vec4 u_original_uv;
uniform vec2 u_source_size;
uniform vec2 u_original_size;
uniform vec2 u_output_size;
uniform float u_frame;
in vec2 v_uv;
out vec4 o_color;
vec4 Source(vec2 p) {
    vec2 h = 0.5 / u_source_size;
    return texture(u_source, u_source_uv.xy + clamp(p, h, 1.0 - h) * u_source_uv.zw);
}
vec4 Original(vec2 p) {
    vec2 h = 0.5 / u_original_size;
    return texture(u_original, u_original_uv.xy + clamp(p, h, 1.0 - h) * u_original_uv.zw);
}
vec4 History(vec2 p) {
    return texture(u_history, p);
}
)";

int TargetSide(float base, float scale) {
    return std::clamp(static_cast<int>(std::lround(base * scale)), 1, MAX_TARGET);
}

} // namespace

bool EffectChain::Load(Device& device, const EffectSpec& spec) {
    Release();
    std::vector<Pass> passes;
    for (const EffectPass& in : spec.passes) {
        Pass pass;
        pass.spec = in;
        pass.program = device.CreateProgram(VERTEX, std::string(DECLARATIONS) + in.fragment, {"a_pos"});
        if (!pass.program) {
            log::Warn("gfx", "effect pass {} does not compile on this device", in.name);
            for (Pass& made : passes) {
                gl::DeleteProgram(made.program);
            }
            return false;
        }
        const auto at = [&pass](const char* name) { return gl::GetUniformLocation(pass.program, name); };
        pass.u_source = at("u_source");
        pass.u_original = at("u_original");
        pass.u_history = at("u_history");
        pass.u_source_uv = at("u_source_uv");
        pass.u_original_uv = at("u_original_uv");
        pass.u_source_size = at("u_source_size");
        pass.u_original_size = at("u_original_size");
        pass.u_output_size = at("u_output_size");
        pass.u_frame = at("u_frame");
        pass.u_flip = at("u_flip");
        for (const std::string& id : spec.params) {
            pass.params.push_back(at(("p_" + id).c_str()));
        }
        passes.push_back(std::move(pass));
    }
    passes_ = std::move(passes);
    values_.assign(spec.params.size(), 0.0f);
    if (!passes_.empty() && !vao_) {
        const float corners[] = {-1, -1, 1, -1, -1, 1, 1, 1};
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
        gl::BufferData(gl::ARRAY_BUFFER, sizeof(corners), corners, gl::STATIC_DRAW);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, 2 * sizeof(float), nullptr);
        gl::BindVertexArray(0);
        gl::BindBuffer(gl::ARRAY_BUFFER, 0);
    }
    return true;
}

void EffectChain::SetParam(std::size_t index, float value) {
    if (index < values_.size()) {
        values_[index] = value;
    }
}

bool EffectChain::Ensure(Device& device, Target& target, int width, int height, bool linear) {
    if (target.framebuffer && target.texture.width == width && target.texture.height == height) {
        return false;
    }
    if (target.texture) {
        device.DestroyTexture(target.texture);
    }
    target.texture = device.CreateTexture(width, height, PixelFormat::Rgba8, nullptr, linear);
    if (!target.framebuffer) {
        gl::GenFramebuffers(1, &target.framebuffer);
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, target.framebuffer);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, target.texture.id, 0);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT);
    return true;
}

bool EffectChain::Draw(Device& device, const Input& input, const Output& output, int region_index) {
    if (passes_.empty() || region_index < 0 || !input.texture || input.width < 1 || input.height < 1 ||
        output.target.w < 1 || output.target.h < 1 || output.view_width <= 0 || output.view_height <= 0) {
        return false;
    }
    if (static_cast<std::size_t>(region_index) >= regions_.size()) {
        regions_.resize(static_cast<std::size_t>(region_index) + 1);
    }
    Region& region = regions_[static_cast<std::size_t>(region_index)];
    region.targets.resize(passes_.size());
    const bool same_picture = region.drawn && region.serial == input.serial;

    gl::Disable(gl::BLEND);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::BindVertexArray(vao_);

    unsigned source = input.texture;
    Rect source_uv = input.uv;
    float source_w = input.width, source_h = input.height;
    const float frame = static_cast<float>(input.frame % FRAME_WRAP);

    for (std::size_t i = 0; i < passes_.size(); ++i) {
        const Pass& pass = passes_[i];
        const bool last = i + 1 == passes_.size();
        PassTargets& targets = region.targets[i];
        float out_w = output.target.w, out_h = output.target.h;
        bool skip = false;
        if (!last) {
            const bool by_output = pass.spec.size == EffectPass::Size::Output;
            const int w = TargetSide(by_output ? output.target.w : input.width, pass.spec.scale_x);
            const int h = TargetSide(by_output ? output.target.h : input.height, pass.spec.scale_y);
            if (pass.spec.feedback) {
                // The picture did not change: what the pass kept is still right, and
                // drawing it again would age its history twice.
                skip = same_picture && targets.now.framebuffer && targets.now.texture.width == w &&
                       targets.now.texture.height == h;
                if (!skip) {
                    std::swap(targets.now, targets.before);
                    Ensure(device, targets.before, w, h, pass.spec.linear);
                }
            }
            if (!skip) {
                Ensure(device, targets.now, w, h, pass.spec.linear);
                gl::BindFramebuffer(gl::FRAMEBUFFER, targets.now.framebuffer);
                gl::Viewport(0, 0, w, h);
            }
            out_w = static_cast<float>(w);
            out_h = static_cast<float>(h);
        } else {
            gl::BindFramebuffer(gl::FRAMEBUFFER, output.framebuffer);
            const int x = static_cast<int>(std::lround(output.target.x));
            const int y = static_cast<int>(std::lround(output.target.y));
            const int w = static_cast<int>(std::lround(output.target.w));
            const int h = static_cast<int>(std::lround(output.target.h));
            gl::Viewport(x, output.view_height - y - h, w, h);
        }
        if (!skip) {
            gl::UseProgram(pass.program);
            gl::ActiveTexture(gl::TEXTURE0 + 2);
            gl::BindTexture(gl::TEXTURE_2D, pass.spec.feedback && !last ? targets.before.texture.id : 0);
            gl::ActiveTexture(gl::TEXTURE0 + 1);
            gl::BindTexture(gl::TEXTURE_2D, input.texture);
            gl::ActiveTexture(gl::TEXTURE0);
            gl::BindTexture(gl::TEXTURE_2D, source);
            gl::Uniform1i(pass.u_source, 0);
            gl::Uniform1i(pass.u_original, 1);
            gl::Uniform1i(pass.u_history, 2);
            gl::Uniform4f(pass.u_source_uv, source_uv.x, source_uv.y, source_uv.w, source_uv.h);
            gl::Uniform4f(pass.u_original_uv, input.uv.x, input.uv.y, input.uv.w, input.uv.h);
            gl::Uniform2f(pass.u_source_size, source_w, source_h);
            gl::Uniform2f(pass.u_original_size, input.width, input.height);
            gl::Uniform2f(pass.u_output_size, out_w, out_h);
            gl::Uniform1f(pass.u_frame, frame);
            gl::Uniform1f(pass.u_flip, last ? 1.0f : 0.0f);
            for (std::size_t p = 0; p < pass.params.size() && p < values_.size(); ++p) {
                if (pass.params[p] >= 0) {
                    gl::Uniform1f(pass.params[p], values_[p]);
                }
            }
            gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        }
        if (!last) {
            source = targets.now.texture.id;
            source_uv = Rect{0, 0, 1, 1};
            source_w = out_w;
            source_h = out_h;
        }
    }
    region.serial = input.serial;
    region.drawn = true;

    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, 0);
    gl::ActiveTexture(gl::TEXTURE0 + 1);
    gl::BindTexture(gl::TEXTURE_2D, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, 0);
    gl::BindVertexArray(0);
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    return true;
}

void EffectChain::Free(Region& region) {
    for (PassTargets& targets : region.targets) {
        for (Target* target : {&targets.now, &targets.before}) {
            if (target->framebuffer) {
                gl::DeleteFramebuffers(1, &target->framebuffer);
            }
            if (target->texture) {
                gl::DeleteTextures(1, &target->texture.id);
            }
            *target = Target{};
        }
    }
    region = Region{};
}

void EffectChain::Release() {
    for (Region& region : regions_) {
        Free(region);
    }
    regions_.clear();
    for (Pass& pass : passes_) {
        if (pass.program) {
            gl::DeleteProgram(pass.program);
        }
    }
    passes_.clear();
    values_.clear();
    if (vbo_) {
        gl::DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        gl::DeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

} // namespace den::gfx
