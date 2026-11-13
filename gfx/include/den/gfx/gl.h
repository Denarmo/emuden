// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The subset of OpenGL 3.3 core / OpenGL ES 3.0 the app uses, loaded at run time, so no
// system GL header is needed and the same code serves both. Add a function by adding a line
// to DEN_GL_FUNCTIONS.

#pragma once

#include <cstddef>
#include <cstdint>

namespace den::gl {

using Enum = unsigned int;
using UInt = unsigned int;
using Int = int;
using Sizei = int;
using Bitfield = unsigned int;
using Boolean = unsigned char;
using Float = float;
using Char = char;
using Sizeiptr = std::ptrdiff_t;
using Intptr = std::ptrdiff_t;

// clang-format off
#define DEN_GL_FUNCTIONS(X) \
    X(void, Viewport, (Int x, Int y, Sizei w, Sizei h)) \
    X(void, Scissor, (Int x, Int y, Sizei w, Sizei h)) \
    X(void, ClearColor, (Float r, Float g, Float b, Float a)) \
    X(void, Clear, (Bitfield mask)) \
    X(void, Enable, (Enum cap)) \
    X(void, Disable, (Enum cap)) \
    X(void, BlendFunc, (Enum src, Enum dst)) \
    X(void, BlendFuncSeparate, (Enum src, Enum dst, Enum src_alpha, Enum dst_alpha)) \
    X(void, BlendEquation, (Enum mode)) \
    X(void, ColorMask, (Boolean r, Boolean g, Boolean b, Boolean a)) \
    X(void, DepthMask, (Boolean flag)) \
    X(void, PixelStorei, (Enum name, Int value)) \
    X(void, GenTextures, (Sizei n, UInt* out)) \
    X(void, DeleteTextures, (Sizei n, const UInt* ids)) \
    X(void, BindTexture, (Enum target, UInt id)) \
    X(void, ActiveTexture, (Enum unit)) \
    X(void, TexImage2D, (Enum target, Int level, Int internal, Sizei w, Sizei h, Int border, Enum format, Enum type, const void* data)) \
    X(void, TexSubImage2D, (Enum target, Int level, Int x, Int y, Sizei w, Sizei h, Enum format, Enum type, const void* data)) \
    X(void, TexParameteri, (Enum target, Enum name, Int value)) \
    X(void, GenerateMipmap, (Enum target)) \
    X(void, GenBuffers, (Sizei n, UInt* out)) \
    X(void, DeleteBuffers, (Sizei n, const UInt* ids)) \
    X(void, BindBuffer, (Enum target, UInt id)) \
    X(void, BufferData, (Enum target, Sizeiptr size, const void* data, Enum usage)) \
    X(void, BufferSubData, (Enum target, Intptr offset, Sizeiptr size, const void* data)) \
    X(void*, MapBufferRange, (Enum target, Intptr offset, Sizeiptr length, Bitfield access)) \
    X(Boolean, UnmapBuffer, (Enum target)) \
    X(void, GenVertexArrays, (Sizei n, UInt* out)) \
    X(void, DeleteVertexArrays, (Sizei n, const UInt* ids)) \
    X(void, BindVertexArray, (UInt id)) \
    X(void, EnableVertexAttribArray, (UInt index)) \
    X(void, DisableVertexAttribArray, (UInt index)) \
    X(void, VertexAttribPointer, (UInt index, Int size, Enum type, Boolean normalized, Sizei stride, const void* offset)) \
    X(void, VertexAttrib4f, (UInt index, Float x, Float y, Float z, Float w)) \
    X(UInt, CreateShader, (Enum type)) \
    X(void, ShaderSource, (UInt shader, Sizei count, const Char* const* text, const Int* length)) \
    X(void, CompileShader, (UInt shader)) \
    X(void, GetShaderiv, (UInt shader, Enum name, Int* out)) \
    X(void, GetShaderInfoLog, (UInt shader, Sizei size, Sizei* length, Char* out)) \
    X(void, DeleteShader, (UInt shader)) \
    X(UInt, CreateProgram, (void)) \
    X(void, AttachShader, (UInt program, UInt shader)) \
    X(void, BindAttribLocation, (UInt program, UInt index, const Char* name)) \
    X(void, LinkProgram, (UInt program)) \
    X(void, GetProgramiv, (UInt program, Enum name, Int* out)) \
    X(void, GetProgramInfoLog, (UInt program, Sizei size, Sizei* length, Char* out)) \
    X(void, UseProgram, (UInt program)) \
    X(void, DeleteProgram, (UInt program)) \
    X(Int, GetUniformLocation, (UInt program, const Char* name)) \
    X(Int, GetAttribLocation, (UInt program, const Char* name)) \
    X(void, Uniform1i, (Int location, Int v)) \
    X(void, Uniform1ui, (Int location, UInt v)) \
    X(void, GetActiveUniform, (UInt program, UInt index, Sizei size, Sizei* length, Int* count, Enum* type, Char* name)) \
    X(void, Uniform1f, (Int location, Float v)) \
    X(void, Uniform2f, (Int location, Float a, Float b)) \
    X(void, Uniform3f, (Int location, Float a, Float b, Float c)) \
    X(void, Uniform4f, (Int location, Float a, Float b, Float c, Float d)) \
    X(void, Uniform4fv, (Int location, Sizei count, const Float* v)) \
    X(void, UniformMatrix4fv, (Int location, Sizei count, Boolean transpose, const Float* m)) \
    X(void, DrawArrays, (Enum mode, Int first, Sizei count)) \
    X(void, DrawElements, (Enum mode, Sizei count, Enum type, const void* indices)) \
    X(void, ReadPixels, (Int x, Int y, Sizei w, Sizei h, Enum format, Enum type, void* out)) \
    X(void, ReadBuffer, (Enum source)) \
    X(Enum, GetError, (void)) \
    X(const unsigned char*, GetString, (Enum name)) \
    X(const unsigned char*, GetStringi, (Enum name, UInt index)) \
    X(void, GetIntegerv, (Enum name, Int* out)) \
    X(void, GenFramebuffers, (Sizei n, UInt* out)) \
    X(void, DeleteFramebuffers, (Sizei n, const UInt* ids)) \
    X(void, BindFramebuffer, (Enum target, UInt id)) \
    X(void, FramebufferTexture2D, (Enum target, Enum attachment, Enum textarget, UInt texture, Int level)) \
    X(Enum, CheckFramebufferStatus, (Enum target)) \
    X(void, BlitFramebuffer, (Int sx0, Int sy0, Int sx1, Int sy1, Int dx0, Int dy0, Int dx1, Int dy1, Bitfield mask, Enum filter)) \
    X(void, GenRenderbuffers, (Sizei n, UInt* out)) \
    X(void, DeleteRenderbuffers, (Sizei n, const UInt* ids)) \
    X(void, BindRenderbuffer, (Enum target, UInt id)) \
    X(void, RenderbufferStorage, (Enum target, Enum format, Sizei w, Sizei h)) \
    X(void, FramebufferRenderbuffer, (Enum target, Enum attachment, Enum rbtarget, UInt rb)) \
    X(void, DepthFunc, (Enum func)) \
    X(void, CullFace, (Enum mode)) \
    X(void, FrontFace, (Enum mode)) \
    X(void, Flush, (void)) \
    X(void, Finish, (void))
// clang-format on

#define DEN_GL_DECLARE(ret, name, args) extern ret(*name) args;
DEN_GL_FUNCTIONS(DEN_GL_DECLARE)
#undef DEN_GL_DECLARE

using ProcLoader = void* (*)(const char* name);
/// Fills the function pointers from the current context. False (with the first missing name
/// in `missing`) when the context lacks one.
bool Load(ProcLoader loader, const char** missing = nullptr);

constexpr Enum COLOR_BUFFER_BIT = 0x4000, DEPTH_BUFFER_BIT = 0x0100, STENCIL_BUFFER_BIT = 0x0400;
constexpr Enum BLEND = 0x0BE2, SCISSOR_TEST = 0x0C11, DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44,
               STENCIL_TEST = 0x0B90, DITHER = 0x0BD0;
constexpr Enum ZERO = 0, ONE = 1, SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr Enum TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0;
constexpr Enum TEXTURE_MAG_FILTER = 0x2800, TEXTURE_MIN_FILTER = 0x2801, TEXTURE_WRAP_S = 0x2802,
               TEXTURE_WRAP_T = 0x2803;
constexpr Enum NEAREST = 0x2600, LINEAR = 0x2601, LINEAR_MIPMAP_LINEAR = 0x2703,
               CLAMP_TO_EDGE = 0x812F, REPEAT = 0x2901;
constexpr Enum UNSIGNED_BYTE = 0x1401, UNSIGNED_SHORT = 0x1403, UNSIGNED_INT = 0x1405,
               FLOAT = 0x1406, UNSIGNED_SHORT_5_6_5 = 0x8363, UNSIGNED_SHORT_5_5_5_1 = 0x8034;
constexpr Enum RED = 0x1903, RGB = 0x1907, RGBA = 0x1908, R8 = 0x8229, RGB8 = 0x8051,
               RGBA8 = 0x8058, RGB565 = 0x8D62, BGRA = 0x80E1;
constexpr Enum UNPACK_ALIGNMENT = 0x0CF5, UNPACK_ROW_LENGTH = 0x0CF2, PACK_ALIGNMENT = 0x0D05;
constexpr Enum ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893, PIXEL_PACK_BUFFER = 0x88EB,
               PIXEL_UNPACK_BUFFER = 0x88EC;
constexpr Enum STATIC_DRAW = 0x88E4, DYNAMIC_DRAW = 0x88E8, STREAM_DRAW = 0x88E0,
               STREAM_READ = 0x88E1;
constexpr Enum FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31, COMPILE_STATUS = 0x8B81,
               LINK_STATUS = 0x8B82, INFO_LOG_LENGTH = 0x8B84;
constexpr Enum TRIANGLES = 0x0004, TRIANGLE_STRIP = 0x0005;
constexpr Enum FRAMEBUFFER = 0x8D40, READ_FRAMEBUFFER = 0x8CA8, DRAW_FRAMEBUFFER = 0x8CA9,
               RENDERBUFFER = 0x8D41, COLOR_ATTACHMENT0 = 0x8CE0, DEPTH_ATTACHMENT = 0x8D00,
               DEPTH_STENCIL_ATTACHMENT = 0x821A, DEPTH24_STENCIL8 = 0x88F0,
               DEPTH_COMPONENT16 = 0x81A5, FRAMEBUFFER_COMPLETE = 0x8CD5,
               FRAMEBUFFER_BINDING = 0x8CA6;
constexpr Enum VENDOR = 0x1F00, RENDERER = 0x1F01, VERSION = 0x1F02, MAX_TEXTURE_SIZE = 0x0D33;
constexpr Enum BACK = 0x0405, MAP_READ_BIT = 0x0001;
constexpr Enum ACTIVE_UNIFORMS = 0x8B86, INT = 0x1404, FLOAT_VEC2 = 0x8B50, FLOAT_VEC3 = 0x8B51,
               FLOAT_VEC4 = 0x8B52, FLOAT_MAT4 = 0x8B5C, SAMPLER_2D = 0x8B5E;
constexpr Enum MIRRORED_REPEAT = 0x8370, CLAMP_TO_BORDER = 0x812D, NEAREST_MIPMAP_NEAREST = 0x2700,
               LINEAR_MIPMAP_NEAREST = 0x2701, NEAREST_MIPMAP_LINEAR = 0x2702;
constexpr Enum FRAMEBUFFER_SRGB = 0x8DB9;
constexpr Enum RGBA32F = 0x8814, RGBA16F = 0x881A, HALF_FLOAT = 0x140B, SRGB8_ALPHA8 = 0x8C43;
constexpr Enum EXTENSIONS = 0x1F03, NUM_EXTENSIONS = 0x821D;
constexpr Boolean FALSE_ = 0, TRUE_ = 1;

} // namespace den::gl
