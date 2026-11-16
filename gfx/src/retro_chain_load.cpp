// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Compiling a preset: every pass's program and what each of its uniforms is, the LUTs and the
// copy program.

#include <algorithm>
#include <string_view>

#include "den/gfx/retro_chain.h"
#include "retro_chain_state.h"
#include "retro_compile.h"

namespace den::gfx {

using namespace retro;

namespace {

// Copies a picture: the input into the chain's Original, a last pass into the frame.
constexpr const char* COPY_VERTEX = R"(
in vec4 a_pos;
uniform vec4 u_uv;
uniform float u_flip;
out vec2 v_uv;
void main() {
    v_uv = u_uv.xy + a_pos.xy * u_uv.zw;
    gl_Position = vec4(a_pos.x * 2.0 - 1.0, (a_pos.y * 2.0 - 1.0) * (1.0 - 2.0 * u_flip), 0.0, 1.0);
}
)";
constexpr const char* COPY_FRAGMENT = R"(
uniform sampler2D u_texture;
in vec2 v_uv;
out vec4 o_color;
void main() {
    o_color = texture(u_texture, v_uv);
}
)";

} // namespace

Status RetroChain::Load(Device& device, const RetroSpec& spec) {
    Release();
    if (spec.passes.empty()) {
        return Status::Fail("the preset has no passes");
    }
    auto state = std::make_unique<State>();
    state->gles = device.Gles();
    if (state->gles) {
        int count = 0;
        gl::GetIntegerv(gl::NUM_EXTENSIONS, &count);
        for (int i = 0; i < count; ++i) {
            const auto* name = reinterpret_cast<const char*>(gl::GetStringi(gl::EXTENSIONS, static_cast<unsigned>(i)));
            if (name && (std::string_view(name) == "GL_EXT_color_buffer_half_float" ||
                         std::string_view(name) == "GL_EXT_color_buffer_float")) {
                state->float_targets = true;
            }
        }
    } else {
        state->float_targets = true;
    }
    for (const RetroParam& param : spec.params) {
        state->param_ids.push_back(param.id);
    }
    state->values = spec.values;
    state->values.resize(spec.params.size(), 0.0f);
    state->lut_specs = spec.luts;
    for (const RetroPassSpec& pass : spec.passes) {
        state->passes.push_back(State::Pass{pass, 0, {}});
    }
    state->feedback.assign(spec.passes.size(), false);

    RetroNames names;
    names.params = state->param_ids;
    for (const RetroPassSpec& pass : spec.passes) {
        names.aliases.push_back(pass.alias);
    }
    for (const RetroLut& lut : spec.luts) {
        names.luts.push_back(lut.name);
    }
    for (std::size_t p = 0; p < state->passes.size(); ++p) {
        State::Pass& pass = state->passes[p];
        GlslStages stages;
        if (spec.language == RetroLanguage::Slang) {
            Result<GlslStages> compiled = CompileSlang(pass.spec.vertex, pass.spec.fragment, state->gles);
            if (!compiled) {
                state->Release();
                return Status::Fail(pass.spec.name + ": " + compiled.error().substr(0, MAX_LOG));
            }
            stages = compiled.take();
        } else {
            stages = PrepareRetroGlsl(pass.spec.vertex, device.ShaderHeader(), state->gles);
        }
        std::string log;
        pass.program = LinkRetroProgram(stages.vertex, stages.fragment, log);
        if (!pass.program) {
            state->Release();
            return Status::Fail(pass.spec.name + ": " + log.substr(0, MAX_LOG));
        }
        gl::UseProgram(pass.program);
        int count = 0;
        gl::GetProgramiv(pass.program, gl::ACTIVE_UNIFORMS, &count);
        int unit = 0;
        for (int i = 0; i < count; ++i) {
            char name[256] = {};
            int length = 0, size = 0;
            gl::Enum type = 0;
            gl::GetActiveUniform(pass.program, static_cast<unsigned>(i), sizeof(name), &length, &size, &type, name);
            const auto sem = ResolveRetroName(name, static_cast<int>(p), names);
            if (!sem) {
                continue;
            }
            Binding binding;
            binding.sem = sem->first;
            binding.index = sem->second;
            binding.location = gl::GetUniformLocation(pass.program, name);
            binding.type = type;
            if (IsTexture(binding.sem)) {
                binding.unit = unit++;
                gl::Uniform1i(binding.location, binding.unit);
            }
            if ((binding.sem == Sem::TexPassFeedback || binding.sem == Sem::PassFeedbackSize) && binding.index >= 0 &&
                static_cast<std::size_t>(binding.index) < state->feedback.size()) {
                state->feedback[static_cast<std::size_t>(binding.index)] = true;
            }
            if (binding.sem == Sem::TexHistory || binding.sem == Sem::HistorySize) {
                state->history = std::clamp(std::max(state->history, binding.index), 0, MAX_HISTORY);
            }
            pass.bindings.push_back(binding);
        }
    }

    for (const RetroLut& lut : spec.luts) {
        Image image;
        if (lut.image.width > 0 && lut.image.height > 0) {
            gl::GenTextures(1, &image.id);
            gl::BindTexture(gl::TEXTURE_2D, image.id);
            gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
            gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<int>(gl::RGBA8), lut.image.width, lut.image.height, 0,
                           gl::RGBA, gl::UNSIGNED_BYTE, lut.image.rgba.data());
            image.width = lut.image.width;
            image.height = lut.image.height;
        }
        state->luts.push_back(image);
    }

    std::string log;
    state->copy = LinkRetroProgram(device.ShaderHeader() + COPY_VERTEX, device.ShaderHeader() + COPY_FRAGMENT, log);
    if (!state->copy) {
        state->Release();
        return Status::Fail("copy: " + log);
    }
    state->copy_uv = gl::GetUniformLocation(state->copy, "u_uv");
    state->copy_flip = gl::GetUniformLocation(state->copy, "u_flip");
    state->copy_texture = gl::GetUniformLocation(state->copy, "u_texture");

    gl::GenVertexArrays(1, &state->vao);
    gl::GenBuffers(1, &state->vbo);
    gl::BindVertexArray(state->vao);
    gl::BindBuffer(gl::ARRAY_BUFFER, state->vbo);
    gl::BufferData(gl::ARRAY_BUFFER, sizeof(QUAD), QUAD, gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 4, gl::FLOAT, gl::FALSE_, 8 * sizeof(float), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 4, gl::FLOAT, gl::FALSE_, 8 * sizeof(float),
                            reinterpret_cast<const void*>(4 * sizeof(float)));
    gl::BindVertexArray(0);
    gl::BindBuffer(gl::ARRAY_BUFFER, 0);
    gl::UseProgram(0);
    state_ = std::move(state);
    return Status{};
}

} // namespace den::gfx
