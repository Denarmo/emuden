// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/device.h"

namespace den::gfx {

namespace {

unsigned int Compile(gl::Enum type, const std::string& source) {
    const unsigned int shader = gl::CreateShader(type);
    const char* text = source.c_str();
    gl::ShaderSource(shader, 1, &text, nullptr);
    gl::CompileShader(shader);
    int ok = 0;
    gl::GetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char info[1024] = {};
        gl::GetShaderInfoLog(shader, sizeof(info) - 1, nullptr, info);
        log::Error("gfx", "shader compile failed: {}", info);
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

void FormatOf(PixelFormat format, gl::Int& internal, gl::Enum& external) {
    internal = format == PixelFormat::R8 ? gl::R8 : gl::RGBA8;
    external = format == PixelFormat::R8 ? gl::RED : gl::RGBA;
}

} // namespace

Status Device::Init(gl::ProcLoader loader, bool gles) {
    gles_ = gles;
    const char* missing = nullptr;
    if (!gl::Load(loader, &missing)) {
        return Status::Fail(std::string("the graphics driver lacks ") + (missing ? missing : "OpenGL"));
    }
    const auto text = [](gl::Enum name) {
        const unsigned char* s = gl::GetString(name);
        return std::string(s ? reinterpret_cast<const char*>(s) : "?");
    };
    log::Info("gfx", "{} | {} | {}", text(gl::VERSION), text(gl::RENDERER), text(gl::VENDOR));
    return {};
}

std::string Device::ShaderHeader() const {
    return gles_ ? "#version 300 es\nprecision highp float;\nprecision mediump sampler2D;\n"
                 : "#version 330 core\n";
}

unsigned int Device::CreateProgram(const std::string& vertex, const std::string& fragment,
                                   const std::vector<const char*>& attributes) {
    const unsigned int vs = Compile(gl::VERTEX_SHADER, ShaderHeader() + vertex);
    const unsigned int fs = Compile(gl::FRAGMENT_SHADER, ShaderHeader() + fragment);
    if (!vs || !fs) {
        return 0;
    }
    const unsigned int program = gl::CreateProgram();
    gl::AttachShader(program, vs);
    gl::AttachShader(program, fs);
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        gl::BindAttribLocation(program, static_cast<unsigned int>(i), attributes[i]);
    }
    gl::LinkProgram(program);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    int ok = 0;
    gl::GetProgramiv(program, gl::LINK_STATUS, &ok);
    if (!ok) {
        char info[1024] = {};
        gl::GetProgramInfoLog(program, sizeof(info) - 1, nullptr, info);
        log::Error("gfx", "shader link failed: {}", info);
        gl::DeleteProgram(program);
        return 0;
    }
    return program;
}

Texture Device::CreateTexture(int width, int height, PixelFormat format, const void* pixels,
                              bool linear) {
    Texture texture{0, width, height};
    gl::GenTextures(1, &texture.id);
    gl::BindTexture(gl::TEXTURE_2D, texture.id);
    gl::Int internal;
    gl::Enum external;
    FormatOf(format, internal, external);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, internal, width, height, 0, external, gl::UNSIGNED_BYTE, pixels);
    const gl::Int filter = linear ? gl::LINEAR : gl::NEAREST;
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    return texture;
}

void Device::UpdateTexture(const Texture& texture, int x, int y, int width, int height,
                           PixelFormat format, const void* pixels) {
    gl::Int internal;
    gl::Enum external;
    FormatOf(format, internal, external);
    gl::BindTexture(gl::TEXTURE_2D, texture.id);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, x, y, width, height, external, gl::UNSIGNED_BYTE, pixels);
}

void Device::DestroyTexture(Texture& texture) {
    if (texture.id) {
        gl::DeleteTextures(1, &texture.id);
    }
    texture = Texture{};
}

void Device::BeginFrame(int width, int height, Color clear) {
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::Viewport(0, 0, width, height);
    gl::Disable(gl::SCISSOR_TEST);
    gl::ClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
    gl::Clear(gl::COLOR_BUFFER_BIT);
}

std::vector<std::uint8_t> Device::ReadPixels(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::ReadBuffer(gl::BACK);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, width, height, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> line(row);
    for (int y = 0; y < height / 2; ++y) {
        std::uint8_t* top = pixels.data() + row * y;
        std::uint8_t* bottom = pixels.data() + row * (height - 1 - y);
        std::memcpy(line.data(), top, row);
        std::memcpy(top, bottom, row);
        std::memcpy(bottom, line.data(), row);
    }
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }
    return pixels;
}

} // namespace den::gfx
