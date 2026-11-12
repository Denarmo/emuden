// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/log.h"
#include "den/gfx/gl.h"
#include "den/player/session.h"
#include "picture_shader.h"

namespace den::player {

namespace {

constexpr const char* VERTEX = R"(
in vec2 a_pos; in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// v_uv runs 0..1 over the picture, row 0 at the top; u_uv places it in the texture.
constexpr const char* COMMON = R"(
uniform sampler2D u_tex;
uniform vec2 u_source;
uniform vec2 u_output;
uniform vec4 u_uv;
in vec2 v_uv;
out vec4 o_color;
vec3 Fetch(vec2 p) {
    return pow(texture(u_tex, u_uv.xy + p * u_uv.zw).rgb, vec3(2.2));
}
vec4 Out(vec3 c) {
    return vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
)";

// Scan lines whose beam widens with brightness, an aperture grille and a soft vignette. With
// fewer than about three output pixels per line there is no room for lines: it fades out.
constexpr const char* CRT = R"(
void main() {
    vec2 texel = v_uv * u_source;
    float row = floor(texel.y) + 0.5;
    vec3 c = Fetch(vec2(v_uv.x, row / u_source.y));
    float lines = u_output.y / u_source.y;
    float strength = clamp((lines - 1.5) / 1.5, 0.0, 1.0);
    float d = fract(texel.y) - 0.5;
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    float beam = exp(-d * d / (0.07 + 0.24 * lum));
    c *= mix(1.0, beam * 1.3, strength);
    float m = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = vec3(m < 0.5 ? 1.0 : 0.78, (m > 0.5 && m < 1.5) ? 1.0 : 0.78, m > 1.5 ? 1.0 : 0.78);
    c *= mix(vec3(1.0), mask * 1.16, strength);
    vec2 q = v_uv - 0.5;
    c *= 1.0 - 0.35 * dot(q, q);
    o_color = Out(c);
}
)";

// Each pixel a cell with a dark line around it, as on a handheld's screen.
constexpr const char* LCD = R"(
void main() {
    vec2 texel = v_uv * u_source;
    vec2 f = fract(texel);
    vec3 c = Fetch((floor(texel) + 0.5) / u_source);
    vec2 px = u_output / u_source;
    float strength = clamp(min(px.x, px.y) - 2.0, 0.0, 1.0);
    vec2 edge = min(f, 1.0 - f) * px;
    vec2 g = smoothstep(0.0, 1.0, edge);
    float grid = mix(0.55, 1.0, g.x * g.y);
    c *= mix(1.0, grid * 1.1, strength);
    o_color = Out(c);
}
)";

} // namespace

const std::vector<PicturePreset>& PicturePresets() {
    static const std::vector<PicturePreset> presets{{"plain", "Plain"}, {"crt", "CRT"}, {"lcd", "LCD"}};
    return presets;
}

bool PictureShader::Has(const std::string& preset) {
    return preset == "crt" || preset == "lcd";
}

PictureShader::Pass* PictureShader::Find(gfx::Device& device, const std::string& preset) {
    Pass* pass = preset == "crt" ? &crt_ : preset == "lcd" ? &lcd_ : nullptr;
    if (!pass || pass->failed) {
        return nullptr;
    }
    if (!pass->program) {
        const std::string fragment = std::string(COMMON) + (pass == &crt_ ? CRT : LCD);
        pass->program = device.CreateProgram(VERTEX, fragment, {"a_pos", "a_uv"});
        if (!pass->program) {
            log::Warn("player", "the {} picture is not available on this device", preset);
            pass->failed = true;
            return nullptr;
        }
        pass->u_tex = gl::GetUniformLocation(pass->program, "u_tex");
        pass->u_source = gl::GetUniformLocation(pass->program, "u_source");
        pass->u_output = gl::GetUniformLocation(pass->program, "u_output");
        pass->u_uv = gl::GetUniformLocation(pass->program, "u_uv");
    }
    return pass;
}

bool PictureShader::Draw(gfx::Device& device, const std::string& preset, const Quad& quad) {
    if (quad.view_width <= 0 || quad.view_height <= 0 || quad.source_width <= 0 ||
        quad.source_height <= 0) {
        return false;
    }
    Pass* pass = Find(device, preset);
    if (!pass) {
        return false;
    }
    if (!vao_) {
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
    }
    const float x0 = quad.target.x / static_cast<float>(quad.view_width) * 2.0f - 1.0f;
    const float x1 = quad.target.Right() / static_cast<float>(quad.view_width) * 2.0f - 1.0f;
    const float y0 = 1.0f - quad.target.y / static_cast<float>(quad.view_height) * 2.0f;
    const float y1 = 1.0f - quad.target.Bottom() / static_cast<float>(quad.view_height) * 2.0f;
    const float vertices[] = {x0, y0, 0, 0, x1, y0, 1, 0, x0, y1, 0, 1, x1, y1, 1, 1};

    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::Viewport(0, 0, quad.view_width, quad.view_height);
    gl::Disable(gl::BLEND);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::UseProgram(pass->program);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, quad.texture);
    gl::Uniform1i(pass->u_tex, 0);
    gl::Uniform2f(pass->u_source, quad.source_width, quad.source_height);
    gl::Uniform2f(pass->u_output, quad.target.w, quad.target.h);
    gl::Uniform4f(pass->u_uv, quad.uv.x, quad.uv.y, quad.uv.w, quad.uv.h);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, sizeof(vertices), vertices, gl::STREAM_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, 4 * sizeof(float), nullptr);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, 4 * sizeof(float),
                            reinterpret_cast<const void*>(2 * sizeof(float)));
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::BindTexture(gl::TEXTURE_2D, 0);
    return true;
}

void PictureShader::Release() {
    for (Pass* pass : {&crt_, &lcd_}) {
        if (pass->program) {
            gl::DeleteProgram(pass->program);
        }
        *pass = Pass{};
    }
    if (vbo_) {
        gl::DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        gl::DeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

} // namespace den::player
