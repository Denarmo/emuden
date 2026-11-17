// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstddef>

#include "den/base/utf8.h"
#include "den/gfx/canvas.h"

namespace den::gfx {

namespace {

constexpr float MODE_IMAGE = 0.0f, MODE_ALPHA = 1.0f, MODE_SOLID = 2.0f;

const char* VERTEX = R"(
in vec2 a_pos; in vec2 a_uv; in vec4 a_box; in vec3 a_params; in vec4 a_color;
uniform vec2 u_view;
out vec2 v_uv; out vec4 v_box; out vec3 v_params; out vec4 v_color;
void main() {
    v_uv = a_uv; v_box = a_box; v_params = a_params; v_color = a_color;
    gl_Position = vec4(a_pos.x / u_view.x * 2.0 - 1.0, 1.0 - a_pos.y / u_view.y * 2.0, 0.0, 1.0);
}
)";

const char* FRAGMENT = R"(
in vec2 v_uv; in vec4 v_box; in vec3 v_params; in vec4 v_color;
uniform sampler2D u_tex;
out vec4 o_color;
void main() {
    vec4 c = v_color;
    if (v_params.z < 0.5) {
        c *= texture(u_tex, v_uv);
    } else if (v_params.z < 1.5) {
        c.a *= texture(u_tex, v_uv).r;
    }
    float r = v_params.x;
    vec2 q = abs(v_box.xy) - v_box.zw + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
    float a = clamp(0.5 - d, 0.0, 1.0);
    if (v_params.y > 0.0) {
        a *= clamp(0.5 + d + v_params.y, 0.0, 1.0);
    }
    o_color = vec4(c.rgb, c.a * a);
}
)";

} // namespace

Status Canvas::Init(Device& device, Text& text) {
    device_ = &device;
    text_ = &text;
    program_ = device.CreateProgram(VERTEX, FRAGMENT, {"a_pos", "a_uv", "a_box", "a_params", "a_color"});
    if (!program_) {
        return Status::Fail("the canvas shader did not compile");
    }
    u_view_ = gl::GetUniformLocation(program_, "u_view");
    u_tex_ = gl::GetUniformLocation(program_, "u_tex");
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const int stride = sizeof(Vertex);
    const int sizes[] = {2, 2, 4, 3, 4};
    std::size_t offset = 0;
    for (unsigned int i = 0; i < 5; ++i) {
        gl::EnableVertexAttribArray(i);
        gl::VertexAttribPointer(i, sizes[i], gl::FLOAT, gl::FALSE_, stride,
                                reinterpret_cast<const void*>(offset));
        offset += sizes[i] * sizeof(float);
    }
    gl::BindVertexArray(0);
    return {};
}

void Canvas::Begin(int pixel_width, int pixel_height, float scale) {
    width_ = pixel_width;
    height_ = pixel_height;
    scale_ = scale > 0.0f ? scale : 1.0f;
    text_->SetScale(scale_);
    vertices_.clear();
    clips_.clear();
    texture_ = 0;
    RestoreState();
}

void Canvas::SetSize(int pixel_width, int pixel_height, float scale) {
    width_ = pixel_width;
    height_ = pixel_height;
    scale_ = scale > 0.0f ? scale : 1.0f;
    text_->SetScale(scale_);
}

void Canvas::RestoreState() {
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::Viewport(0, 0, width_, height_);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(program_);
    gl::Uniform2f(u_view_, static_cast<float>(width_), static_cast<float>(height_));
    gl::Uniform1i(u_tex_, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    ApplyClip();
}

void Canvas::End() {
    Flush();
    gl::Disable(gl::SCISSOR_TEST);
    gl::BindVertexArray(0);
}

void Canvas::Flush() {
    if (vertices_.empty()) {
        return;
    }
    gl::BindTexture(gl::TEXTURE_2D, texture_ ? texture_ : text_->Atlas().id);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::Sizeiptr>(vertices_.size() * sizeof(Vertex)),
                   vertices_.data(), gl::STREAM_DRAW);
    gl::DrawArrays(gl::TRIANGLES, 0, static_cast<int>(vertices_.size()));
    vertices_.clear();
}

void Canvas::Quad(const Rect& rect, const Rect& uv, Color color, float radius, float border,
                  float mode, unsigned int texture) {
    if (rect.w <= 0 || rect.h <= 0 || color.a == 0) {
        return;
    }
    if (texture != 0 && texture != texture_) {
        Flush();
        texture_ = texture;
    }
    const float s = scale_;
    const float x0 = rect.x * s, y0 = rect.y * s, x1 = rect.Right() * s, y1 = rect.Bottom() * s;
    const float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
    const float rad = std::min(radius * s, std::min(hw, hh));
    const float r = color.r / 255.0f, g = color.g / 255.0f, b = color.b / 255.0f, a = color.a / 255.0f;
    const auto vertex = [&](float x, float y, float u, float v, float lx, float ly) {
        vertices_.push_back(Vertex{x, y, u, v, lx, ly, hw, hh, rad, border * s, mode, r, g, b, a});
    };
    vertex(x0, y0, uv.x, uv.y, -hw, -hh);
    vertex(x1, y0, uv.Right(), uv.y, hw, -hh);
    vertex(x1, y1, uv.Right(), uv.Bottom(), hw, hh);
    vertex(x0, y0, uv.x, uv.y, -hw, -hh);
    vertex(x1, y1, uv.Right(), uv.Bottom(), hw, hh);
    vertex(x0, y1, uv.x, uv.Bottom(), -hw, hh);
}

void Canvas::FillRect(const Rect& rect, Color color, float radius) {
    Quad(rect, Rect{}, color, radius, 0, MODE_SOLID, 0);
}

void Canvas::StrokeRect(const Rect& rect, Color color, float thickness, float radius) {
    Quad(rect, Rect{}, color, radius, thickness, MODE_SOLID, 0);
}

void Canvas::DrawImage(const Rect& rect, const Texture& texture, Color tint, float radius,
                       const Rect& uv) {
    if (!texture) {
        return;
    }
    Quad(rect, uv, tint, radius, 0, MODE_IMAGE, texture.id);
}

float Canvas::DrawText(std::string_view text, float x, float y, float size, Color color, int font) {
    const unsigned int atlas = text_->Atlas().id;
    if (texture_ != 0 && texture_ != atlas) {
        Flush();
    }
    texture_ = atlas;
    const int pixel_size = std::max(4, static_cast<int>(std::lround(size * scale_)));
    const FontMetrics metrics = text_->Metrics(size, font);
    float pen = std::round(x * scale_);
    const float baseline = std::round((y + metrics.ascent) * scale_);
    const float start = pen;
    const float inv = 1.0f / scale_;
    for (std::size_t at = 0; at < text.size();) {
        const Glyph* glyph = text_->GetGlyph(NextCodePoint(text, at), pixel_size, font);
        if (!glyph) {
            continue;
        }
        if (glyph->width > 0) {
            const Rect rect{(pen + glyph->left) * inv, (baseline - glyph->top) * inv,
                            glyph->width * inv, glyph->height * inv};
            Quad(rect, Rect{glyph->u0, glyph->v0, glyph->u1 - glyph->u0, glyph->v1 - glyph->v0},
                 color, 0, 0, MODE_ALPHA, atlas);
        }
        pen += glyph->advance;
    }
    return (pen - start) * inv;
}

void Canvas::ApplyClip() {
    if (clips_.empty()) {
        gl::Disable(gl::SCISSOR_TEST);
        return;
    }
    const Rect& c = clips_.back();
    gl::Enable(gl::SCISSOR_TEST);
    const int x = static_cast<int>(std::floor(c.x * scale_));
    const int y = static_cast<int>(std::floor(c.y * scale_));
    const int w = static_cast<int>(std::ceil(c.w * scale_));
    const int h = static_cast<int>(std::ceil(c.h * scale_));
    gl::Scissor(x, height_ - y - h, std::max(0, w), std::max(0, h));
}

void Canvas::PushClip(const Rect& rect) {
    Flush();
    clips_.push_back(clips_.empty() ? rect : clips_.back().Intersect(rect));
    ApplyClip();
}

void Canvas::PopClip() {
    Flush();
    if (!clips_.empty()) {
        clips_.pop_back();
    }
    ApplyClip();
}

Rect Canvas::CurrentClip() const {
    return clips_.empty() ? Rect{0, 0, Width(), Height()} : clips_.back();
}

Rect CoverUv(int tex_w, int tex_h, const Rect& rect) {
    if (tex_w <= 0 || tex_h <= 0 || rect.w <= 0 || rect.h <= 0) {
        return Rect{0, 0, 1, 1};
    }
    const float image = static_cast<float>(tex_w) / tex_h;
    const float box = rect.w / rect.h;
    if (image > box) {
        const float w = box / image;
        return Rect{(1 - w) * 0.5f, 0, w, 1};
    }
    const float h = image / box;
    return Rect{0, (1 - h) * 0.5f, 1, h};
}

Rect FitRect(int tex_w, int tex_h, const Rect& rect) {
    if (tex_w <= 0 || tex_h <= 0) {
        return rect;
    }
    const float scale = std::min(rect.w / tex_w, rect.h / tex_h);
    const float w = tex_w * scale, h = tex_h * scale;
    return Rect{rect.x + (rect.w - w) * 0.5f, rect.y + (rect.h - h) * 0.5f, w, h};
}

} // namespace den::gfx
