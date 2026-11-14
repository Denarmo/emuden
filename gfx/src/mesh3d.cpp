// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/gfx/mesh3d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "den/gfx/canvas.h"
#include "den/gfx/device.h"
#include "den/gfx/gl.h"

namespace den::gfx {

namespace {

constexpr gl::Enum CW = 0x0900, CCW = 0x0901, LEQUAL = 0x0203;
constexpr float FIELD_OF_VIEW = 0.52f; // radians, about 30 degrees: a mild perspective
constexpr float SUPERSAMPLE = 2.0f;    // offscreen pixels per screen pixel, each way
constexpr int MAX_TARGET = 4096;
constexpr int FADE_BANDS = 8;

const char* VERTEX = R"(
in vec3 a_pos; in vec3 a_normal; in vec2 a_uv; in float a_part;
uniform mat4 u_view_proj; uniform mat4 u_model; uniform mat4 u_rot;
out vec3 v_n; out vec2 v_uv; out float v_part;
void main() {
    v_n = (u_rot * vec4(a_normal, 0.0)).xyz;
    v_uv = a_uv;
    v_part = a_part;
    gl_Position = u_view_proj * u_model * vec4(a_pos, 1.0);
}
)";

// Plastic, paper and metal under a key light from the upper left, a cool fill from the right
// and a rim light from behind, over a sky-to-floor ambient.
const char* FRAGMENT = R"(
in vec3 v_n; in vec2 v_uv; in float v_part;
uniform vec4 u_shell; uniform vec4 u_label; uniform vec4 u_spine;
uniform float u_light; uniform int u_has_tex; uniform int u_clear;
uniform sampler2D u_tex;
// The other faces' pictures: back, spine, cart, disc; bit i of u_arts says art i is there.
uniform sampler2D u_back_art; uniform sampler2D u_spine_art; uniform sampler2D u_cart_art;
uniform sampler2D u_disc_art; uniform int u_arts; uniform vec4 u_cart_uv; uniform vec4 u_sticker;
uniform float u_boot; uniform int u_foil;
float grain(vec2 p) { return fract(sin(dot(floor(p), vec2(12.9898, 78.233))) * 43758.5453); }
out vec4 o_color;
vec4 cover(vec4 r) { return texture(u_tex, r.xy + clamp(v_uv, 0.0, 1.0) * r.zw); }
bool has_art(int bit) { return (u_arts / bit) % 2 == 1; }
void main() {
    vec3 n = normalize(v_n);
    if (!gl_FrontFacing) n = -n;
    vec3 v = vec3(0.0, 0.0, 1.0);
    int part = int(v_part + 0.5);
    vec3 base = u_shell.rgb;
    float alpha = 1.0, gloss = 28.0, spec = 0.28, fresnel = 0.0, diffuse_k = 1.0;
    if (part == 1) {
        base = u_has_tex == 1 ? cover(u_label).rgb : vec3(0.92);
        gloss = 6.0; spec = 0.05;
    } else if (part == 2) {
        base = has_art(2) ? texture(u_spine_art, clamp(v_uv, 0.0, 1.0)).rgb * 0.92
             : u_has_tex == 1 ? cover(u_spine).rgb * 0.88 : u_shell.rgb;
        gloss = 90.0; spec = 0.7; fresnel = 0.35;
    } else if (part == 3) {
        float r = length(v_uv - 0.5) * 2.0;
        if (has_art(8)) {
            vec4 d = texture(u_disc_art, clamp(v_uv, 0.0, 1.0));
            base = mix(vec3(0.72, 0.74, 0.78), d.rgb, d.a);
            gloss = mix(120.0, 16.0, d.a); spec = mix(1.1, 0.14, d.a);
        } else if (r < 0.34) {
            base = mix(vec3(0.78, 0.8, 0.84), vec3(0.62, 0.65, 0.7), smoothstep(0.30, 0.34, r));
            gloss = 120.0; spec = 1.1; fresnel = 0.4;
        } else {
            base = u_has_tex == 1 ? cover(u_label).rgb : vec3(0.85);
            gloss = 14.0; spec = 0.12;
        }
    } else if (part == 4) {
        base = u_shell.rgb * 0.42;
        gloss = 18.0; spec = 0.18;
    } else if (part == 5) {
        base = u_has_tex == 1 ? cover(u_label).rgb : vec3(0.9);
        gloss = 110.0; spec = 0.85; fresnel = 0.55;
    } else if (part == 6) {
        base = vec3(0.82, 0.64, 0.28);
        gloss = 70.0; spec = 1.0; diffuse_k = 0.6;
    } else if (part == 7) {
        base = vec3(0.86, 0.9, 0.96);
        gloss = 140.0; spec = 1.3; fresnel = 0.9; alpha = 0.14;
    } else if (part == 8) {
        float r = length(v_uv - 0.5) * 2.0;
        vec3 rainbow = 0.5 + 0.5 * cos(6.2832 * (r * 2.2 + n.x * 0.6 + vec3(0.0, 0.33, 0.67)));
        base = mix(vec3(0.72, 0.74, 0.78), rainbow, 0.35);
        gloss = 90.0; spec = 1.2; fresnel = 0.5;
    } else if (part == 9) {
        base = vec3(0.93, 0.93, 0.9);
        gloss = 6.0; spec = 0.04;
    } else if (part == 10) {
        gloss = 80.0; spec = 0.7; fresnel = 0.6; alpha = u_shell.a;
    } else if (part == 11) {
        vec2 g = fract(v_uv * vec2(46.0, 9.0));
        float trace = step(0.82, g.x) * step(0.35, g.y) + step(0.9, g.y) * 0.6;
        base = mix(vec3(0.13, 0.3, 0.14), vec3(0.62, 0.55, 0.26), clamp(trace, 0.0, 1.0) * 0.45);
        gloss = 30.0; spec = 0.3;
    } else if (part == 12) {
        base = vec3(0.74, 0.75, 0.78);
        gloss = 55.0; spec = 0.75; diffuse_k = 0.75;
    } else if (part == 13) {
        if (has_art(1)) {
            base = texture(u_back_art, clamp(v_uv, 0.0, 1.0)).rgb;
            gloss = 80.0; spec = 0.5; fresnel = 0.35;
        }
    } else if (part == 15) {
        base = vec3(0.05, 0.06, 0.05);
        gloss = 140.0; spec = 1.0; fresnel = 0.5;
        if (u_boot >= 0.0) {
            // The backlight comes up, then the cover slides down to the middle and rests.
            vec3 lit = vec3(0.62, 0.7, 0.6) * clamp(u_boot, 0.0, 1.0);
            float slide = clamp(u_boot - 1.0, 0.0, 1.0);
            vec2 logo = vec2(0.5, mix(-0.3, 0.5, slide));
            vec2 q = (v_uv - logo) / vec2(0.62, 0.62) + 0.5;
            vec3 c = lit;
            if (u_has_tex == 1 && slide > 0.0 && all(greaterThanEqual(q, vec2(0.0))) && all(lessThanEqual(q, vec2(1.0)))) {
                c = mix(lit, texture(u_tex, u_label.xy + q * u_label.zw).rgb, clamp(u_boot, 0.0, 1.0));
            }
            o_color = vec4(c * u_light + vec3(0.04), 1.0);
            return;
        }
    } else if (part == 16) {
        base = u_boot >= 0.0 ? vec3(1.0, 0.18, 0.12) : vec3(0.25, 0.05, 0.04);
        if (u_boot >= 0.0) {
            o_color = vec4(base * u_light, 1.0);
            return;
        }
    } else if (part == 17) {
        base = u_has_tex == 1 ? cover(u_label).rgb : vec3(0.85);
        float g = grain(v_uv * 900.0) - 0.5;
        base *= 1.0 + g * 0.05;
        gloss = 45.0; spec = 0.32; fresnel = 0.25;
        if (u_foil == 1) {
            // Holographic foil: a rainbow that moves with the angle, strongest on the light parts.
            float k = dot(base, vec3(0.333));
            vec3 rainbow = 0.5 + 0.5 * cos(6.2832 * (v_uv.x * 1.3 + v_uv.y * 0.7 + n.x * 1.6 + n.y + vec3(0.0, 0.33, 0.67)));
            base = mix(base, base * 0.55 + rainbow * 0.6, 0.35 * k + 0.12);
            gloss = 70.0; spec = 0.9; fresnel = 0.6;
        }
    } else if (part == 14) {
        vec4 p = has_art(4) ? texture(u_cart_art, u_cart_uv.xy + clamp(v_uv, 0.0, 1.0) * u_cart_uv.zw) : vec4(0.0);
        base = mix(u_shell.rgb, p.rgb, p.a);
        gloss = 36.0; spec = 0.3;
        vec2 s = (v_uv - u_sticker.xy) / max(u_sticker.zw, vec2(0.0001));
        if (u_has_tex == 1 && u_sticker.z > 0.0 && all(greaterThanEqual(s, vec2(0.0))) && all(lessThanEqual(s, vec2(1.0)))) {
            base = texture(u_tex, u_label.xy + s * u_label.zw).rgb;
            gloss = 8.0; spec = 0.06;
        }
    }
    vec3 key = normalize(vec3(-0.45, 0.6, 0.66));
    vec3 fill = normalize(vec3(0.8, 0.05, 0.45));
    vec3 rim = normalize(vec3(0.25, 0.45, -0.85));
    float ambient = mix(0.22, 0.42, n.y * 0.5 + 0.5);
    float lit = ambient + diffuse_k * (max(dot(n, key), 0.0) * 0.78 + max(dot(n, fill), 0.0) * 0.22);
    float s = pow(max(dot(n, normalize(key + v)), 0.0), gloss) * spec
            + pow(max(dot(n, normalize(fill + v)), 0.0), gloss) * spec * 0.3;
    float edge = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    s += edge * (fresnel * 0.5 + 0.12 * max(dot(n, rim) + 0.4, 0.0));
    vec3 c = base * lit + vec3(s);
    if (u_clear == 1) {
        alpha = clamp(alpha + edge * 0.5 + s * 0.6, 0.0, 0.9);
    }
    o_color = vec4(c * u_light, alpha);
}
)";

} // namespace

Mat4 Mat4::Translate(float x, float y, float z) {
    Mat4 r;
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 Mat4::Scale(float x, float y, float z) {
    Mat4 r;
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

Mat4 Mat4::RotateX(float a) {
    Mat4 r;
    r.m[5] = std::cos(a);
    r.m[6] = std::sin(a);
    r.m[9] = -std::sin(a);
    r.m[10] = std::cos(a);
    return r;
}

Mat4 Mat4::RotateY(float a) {
    Mat4 r;
    r.m[0] = std::cos(a);
    r.m[2] = -std::sin(a);
    r.m[8] = std::sin(a);
    r.m[10] = std::cos(a);
    return r;
}

Mat4 Mat4::RotateZ(float a) {
    Mat4 r;
    r.m[0] = std::cos(a);
    r.m[1] = std::sin(a);
    r.m[4] = -std::sin(a);
    r.m[5] = std::cos(a);
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += m[k * 4 + row] * o.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Mesh3D::~Mesh3D() {
    if (vbo_ != 0) {
        gl::DeleteBuffers(1, &vbo_);
        gl::DeleteVertexArrays(1, &vao_);
    }
}

void Mesh3D::Set(const std::vector<MeshVertex>& vertices) {
    // Made mid-frame, it must leave the canvas's buffers bound, or the canvas writes into it.
    gl::Int vao = 0, buffer = 0;
    gl::GetIntegerv(gl::VERTEX_ARRAY_BINDING, &vao);
    gl::GetIntegerv(gl::ARRAY_BUFFER_BINDING, &buffer);
    if (vao_ == 0) {
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
        const int sizes[] = {3, 3, 2, 1};
        std::size_t offset = 0;
        for (unsigned int i = 0; i < 4; ++i) {
            gl::EnableVertexAttribArray(i);
            gl::VertexAttribPointer(i, sizes[i], gl::FLOAT, gl::FALSE_, sizeof(MeshVertex),
                                    reinterpret_cast<const void*>(offset));
            offset += sizes[i] * sizeof(float);
        }
    } else {
        gl::BindVertexArray(vao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    }
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::Sizeiptr>(vertices.size() * sizeof(MeshVertex)),
                   vertices.data(), gl::STATIC_DRAW);
    gl::BindVertexArray(static_cast<unsigned int>(vao));
    gl::BindBuffer(gl::ARRAY_BUFFER, static_cast<unsigned int>(buffer));
    count_ = static_cast<int>(vertices.size());
}

Status Renderer3D::Init(Device& device) {
    program_ = device.CreateProgram(VERTEX, FRAGMENT, {"a_pos", "a_normal", "a_uv", "a_part"});
    if (!program_) {
        return Status::Fail("the 3D shader did not compile");
    }
    const auto at = [&](const char* name) { return gl::GetUniformLocation(program_, name); };
    u_view_proj_ = at("u_view_proj");
    u_model_ = at("u_model");
    u_rot_ = at("u_rot");
    u_shell_ = at("u_shell");
    u_label_ = at("u_label");
    u_spine_ = at("u_spine");
    u_light_ = at("u_light");
    u_has_tex_ = at("u_has_tex");
    u_tex_ = at("u_tex");
    u_clear_ = at("u_clear");
    u_arts_ = at("u_arts");
    u_cart_uv_ = at("u_cart_uv");
    u_sticker_ = at("u_sticker");
    u_boot_ = at("u_boot");
    u_foil_ = at("u_foil");
    const char* arts[] = {"u_back_art", "u_spine_art", "u_cart_art", "u_disc_art"};
    for (int i = 0; i < 4; ++i) {
        u_art_[i] = at(arts[i]);
    }
    gl::GenFramebuffers(1, &fbo_);
    gl::GenTextures(1, &color_);
    gl::GenRenderbuffers(1, &depth_);
    return {};
}

// Grows the offscreen target to at least this size; it never shrinks, Begin draws in a corner.
void Renderer3D::Target(int width, int height) {
    if (width <= fbo_w_ && height <= fbo_h_) {
        return;
    }
    fbo_w_ = std::max(width, fbo_w_);
    fbo_h_ = std::max(height, fbo_h_);
    gl::BindTexture(gl::TEXTURE_2D, color_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<int>(gl::RGBA8), fbo_w_, fbo_h_, 0, gl::RGBA, gl::UNSIGNED_BYTE,
                   nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<int>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<int>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<int>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<int>(gl::CLAMP_TO_EDGE));
    gl::BindRenderbuffer(gl::RENDERBUFFER, depth_);
    gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT16, fbo_w_, fbo_h_);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, color_, 0);
    gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, depth_);
}

Mat4 Renderer3D::Model(float x, float y, float z, float yaw, float pitch, float roll) {
    return Mat4::Translate(x, y, z) * Mat4::Scale(1, -1, 1) * Mat4::RotateY(yaw) * Mat4::RotateX(pitch) *
           Mat4::RotateZ(roll);
}

void Renderer3D::Begin(Canvas& canvas, const Rect& clip) {
    canvas.Flush();
    const float w = canvas.Width(), h = canvas.Height();
    clip_ = clip.Intersect(Rect{0, 0, w, h});
    const float pixels = canvas.Scale() * SUPERSAMPLE;
    draw_w_ = std::clamp(static_cast<int>(clip_.w * pixels), 1, MAX_TARGET);
    draw_h_ = std::clamp(static_cast<int>(clip_.h * pixels), 1, MAX_TARGET);
    Target(draw_w_, draw_h_);

    const float distance = h * 0.5f / std::tan(FIELD_OF_VIEW * 0.5f);
    const float near_z = distance * 0.2f, far_z = distance * 3.0f;
    const float f = 1.0f / std::tan(FIELD_OF_VIEW * 0.5f);
    Mat4 proj;
    proj.m[0] = f * h / w;
    proj.m[5] = f;
    proj.m[10] = (far_z + near_z) / (near_z - far_z);
    proj.m[11] = -1;
    proj.m[14] = 2 * far_z * near_z / (near_z - far_z);
    proj.m[15] = 0;
    // Only `clip` of the whole view goes into the target: stretch that part to fill it.
    const float x0 = clip_.x / w * 2 - 1, x1 = clip_.Right() / w * 2 - 1;
    const float y0 = 1 - clip_.Bottom() / h * 2, y1 = 1 - clip_.y / h * 2;
    Mat4 crop = Mat4::Scale(2 / (x1 - x0), 2 / (y1 - y0), 1);
    crop.m[12] = -(x0 + x1) / (x1 - x0);
    crop.m[13] = -(y0 + y1) / (y1 - y0);
    view_proj_ = crop * proj * Mat4::Translate(0, 0, -distance) * Mat4::Scale(1, -1, 1) *
                 Mat4::Translate(-w * 0.5f, -h * 0.5f, 0);

    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, draw_w_, draw_h_);
    gl::Disable(gl::SCISSOR_TEST);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
    gl::DepthMask(gl::TRUE_);
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(LEQUAL);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::Disable(gl::BLEND);
    gl::UseProgram(program_);
    gl::UniformMatrix4fv(u_view_proj_, 1, gl::FALSE_, view_proj_.m);
    gl::Uniform1i(u_tex_, 0);
    for (int i = 0; i < 4; ++i) {
        gl::Uniform1i(u_art_[i], i + 1);
    }
    gl::ActiveTexture(gl::TEXTURE0);
}

void Renderer3D::Submit(const Mesh3D& mesh, const Mat4& model, const Material& material, bool mirrored) {
    if (mesh.Empty()) {
        return;
    }
    gl::FrontFace(mirrored ? CW : CCW);
    // Normals turn with the model, without the canvas's y flip, so the light stays above.
    Mat4 rot = Mat4::Scale(1, -1, 1) * model;
    rot.m[12] = rot.m[13] = rot.m[14] = 0;
    gl::UniformMatrix4fv(u_model_, 1, gl::FALSE_, model.m);
    gl::UniformMatrix4fv(u_rot_, 1, gl::FALSE_, rot.m);
    const Color c = material.shell;
    gl::Uniform4f(u_shell_, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
    const Rect& l = material.label_uv;
    const Rect& s = material.spine_uv;
    gl::Uniform4f(u_label_, l.x, l.y, l.w, l.h);
    gl::Uniform4f(u_spine_, s.x, s.y, s.w, s.h);
    gl::Uniform1f(u_light_, material.light);
    gl::Uniform1i(u_has_tex_, material.cover ? 1 : 0);
    const Texture* arts[] = {&material.back_art, &material.spine_art, &material.cart_art, &material.disc_art};
    int present = 0;
    for (int i = 0; i < 4; ++i) {
        if (*arts[i]) {
            present |= 1 << i;
            gl::ActiveTexture(gl::TEXTURE0 + 1 + i);
            gl::BindTexture(gl::TEXTURE_2D, arts[i]->id);
        }
    }
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(u_arts_, present);
    const Rect& cu = material.cart_uv;
    gl::Uniform4f(u_cart_uv_, cu.x, cu.y, cu.w, cu.h);
    const Rect& st = material.sticker;
    gl::Uniform4f(u_sticker_, st.x, st.y, st.w, st.h);
    gl::Uniform1f(u_boot_, material.boot);
    gl::Uniform1i(u_foil_, material.foil ? 1 : 0);
    gl::BindTexture(gl::TEXTURE_2D, material.cover.id);
    gl::BindVertexArray(mesh.vao_);
    gl::DrawArrays(gl::TRIANGLES, 0, mesh.count_);
    gl::BindVertexArray(0);
}

void Renderer3D::Draw(const Mesh3D& mesh, const Mat4& model, const Material& material, bool mirrored) {
    gl::Uniform1i(u_clear_, 0);
    Submit(mesh, model, material, mirrored);
}

void Renderer3D::DrawClear(const Mesh3D& mesh, const Mat4& model, const Material& material, bool mirrored) {
    // Both sides of clear plastic show; it does not hide what is behind it.
    gl::Disable(gl::CULL_FACE);
    gl::DepthMask(gl::FALSE_);
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    gl::Uniform1i(u_clear_, 1);
    Submit(mesh, model, material, mirrored);
    gl::Disable(gl::BLEND);
    gl::DepthMask(gl::TRUE_);
    gl::Enable(gl::CULL_FACE);
}

void Renderer3D::End(Canvas& canvas, float top, float bottom) {
    gl::FrontFace(CCW);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::DEPTH_TEST);
    canvas.RestoreState();
    // The target is upside down to the canvas, and only its corner was drawn.
    const float uw = static_cast<float>(draw_w_) / fbo_w_, uh = static_cast<float>(draw_h_) / fbo_h_;
    const Texture texture{color_, fbo_w_, fbo_h_};
    const int bands = top == bottom ? 1 : FADE_BANDS;
    for (int i = 0; i < bands; ++i) {
        const float a = static_cast<float>(i) / bands, b = static_cast<float>(i + 1) / bands;
        const float alpha = top + (bottom - top) * (a + b) * 0.5f;
        const Rect part{clip_.x, clip_.y + clip_.h * a, clip_.w, clip_.h * (b - a)};
        const Rect uv{0, uh * (1 - a), uw, -uh * (b - a)};
        canvas.DrawImage(part, texture, Color{255, 255, 255, 255}.WithAlpha(alpha), 0, uv);
    }
    canvas.Flush(); // before the target is drawn into again
}

} // namespace den::gfx
