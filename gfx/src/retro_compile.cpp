// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "retro_compile.h"

#include <exception>
#include <mutex>
#include <sstream>
#include <vector>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv_glsl.hpp>

#include "den/gfx/gl.h"

namespace den::gfx {

namespace {

std::string WithoutVersion(const std::string& source) {
    std::istringstream lines(source);
    std::string line, out;
    while (std::getline(lines, line)) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 8, "#version") == 0) {
            out += '\n'; // keeps the line numbers of the compiler's messages
            continue;
        }
        out += line;
        out += '\n';
    }
    return out;
}

struct Spirv {
    std::vector<unsigned> vertex, fragment;
};

Result<Spirv> ToSpirv(const std::string& vertex, const std::string& fragment) {
    static std::once_flag started;
    std::call_once(started, [] { glslang::InitializeProcess(); });
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader vs(EShLangVertex), fs(EShLangFragment);
    // TShader keeps the address of the list of strings until it parses.
    const char* sources[2] = {vertex.c_str(), fragment.c_str()};
    const auto prepare = [](glslang::TShader& shader, EShLanguage stage, const char* const& source) {
        shader.setStrings(&source, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    };
    prepare(vs, EShLangVertex, sources[0]);
    prepare(fs, EShLangFragment, sources[1]);
    if (!vs.parse(GetDefaultResources(), 450, false, messages)) {
        return Result<Spirv>::Fail(std::string("vertex stage: ") + vs.getInfoLog());
    }
    if (!fs.parse(GetDefaultResources(), 450, false, messages)) {
        return Result<Spirv>::Fail(std::string("fragment stage: ") + fs.getInfoLog());
    }
    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&fs);
    if (!program.link(messages)) {
        return Result<Spirv>::Fail(std::string("link: ") + program.getInfoLog());
    }
    Spirv spirv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), spirv.vertex);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), spirv.fragment);
    return spirv;
}

Result<std::string> ToGlsl(std::vector<unsigned> spirv, bool vertex, bool gles) {
    try {
        spirv_cross::CompilerGLSL compiler(std::move(spirv));
        spirv_cross::CompilerGLSL::Options options = compiler.get_common_options();
        options.es = gles;
        options.version = gles ? 300 : 330;
        options.vulkan_semantics = false;
        options.enable_420pack_extension = false;
        options.emit_uniform_buffer_as_plain_uniforms = true;
        options.emit_push_constant_as_uniform_buffer = false;
        options.vertex.fixup_clipspace = false;
        options.vertex.flip_vert_y = false;
        options.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
        options.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
        compiler.set_common_options(options);

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        // Vulkan matches the stages by location, GL by name.
        for (const spirv_cross::Resource& io : vertex ? resources.stage_outputs : resources.stage_inputs) {
            const unsigned location = compiler.get_decoration(io.id, spv::DecorationLocation);
            compiler.set_name(io.id, "ra_io" + std::to_string(location));
        }
        const auto unbind = [&compiler](const spirv_cross::SmallVector<spirv_cross::Resource>& list) {
            for (const spirv_cross::Resource& resource : list) {
                compiler.unset_decoration(resource.id, spv::DecorationBinding);
                compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
            }
        };
        unbind(resources.uniform_buffers);
        unbind(resources.sampled_images);
        return compiler.compile();
    } catch (const std::exception& e) {
        return Result<std::string>::Fail(std::string(vertex ? "vertex" : "fragment") + " stage: " + e.what());
    }
}


unsigned CompileStage(gl::Enum type, const std::string& source, std::string& log) {
    const unsigned shader = gl::CreateShader(type);
    const char* text = source.c_str();
    gl::ShaderSource(shader, 1, &text, nullptr);
    gl::CompileShader(shader);
    int ok = 0;
    gl::GetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char buffer[2048] = {};
        gl::GetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
        log = std::string(type == gl::VERTEX_SHADER ? "vertex: " : "fragment: ") + buffer;
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

unsigned LinkRetroProgram(const std::string& vertex, const std::string& fragment, std::string& log) {
    const unsigned vs = CompileStage(gl::VERTEX_SHADER, vertex, log);
    if (!vs) {
        return 0;
    }
    const unsigned fs = CompileStage(gl::FRAGMENT_SHADER, fragment, log);
    if (!fs) {
        gl::DeleteShader(vs);
        return 0;
    }
    const unsigned program = gl::CreateProgram();
    gl::AttachShader(program, vs);
    gl::AttachShader(program, fs);
    // The older GLSL shaders' attributes; Slang's carry their own locations.
    gl::BindAttribLocation(program, 0, "VertexCoord");
    gl::BindAttribLocation(program, 1, "TexCoord");
    gl::BindAttribLocation(program, 2, "COLOR");
    gl::BindAttribLocation(program, 0, "a_pos");
    gl::LinkProgram(program);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    int ok = 0;
    gl::GetProgramiv(program, gl::LINK_STATUS, &ok);
    if (!ok) {
        char buffer[2048] = {};
        gl::GetProgramInfoLog(program, sizeof(buffer), nullptr, buffer);
        log = std::string("link: ") + buffer;
        gl::DeleteProgram(program);
        return 0;
    }
    return program;
}

GlslStages PrepareRetroGlsl(const std::string& source, const std::string& header) {
    const std::string body = WithoutVersion(source);
    const std::string common = "#define PARAMETER_UNIFORM 1\n";
    return GlslStages{header + "#define VERTEX 1\n" + common + body, header + "#define FRAGMENT 1\n" + common + body};
}

Result<GlslStages> CompileSlang(const std::string& vertex, const std::string& fragment, bool gles) {
    Result<Spirv> spirv = ToSpirv(vertex, fragment);
    if (!spirv) {
        return spirv.status();
    }
    Result<std::string> vertex_glsl = ToGlsl(std::move(spirv->vertex), true, gles);
    if (!vertex_glsl) {
        return vertex_glsl.status();
    }
    Result<std::string> fragment_glsl = ToGlsl(std::move(spirv->fragment), false, gles);
    if (!fragment_glsl) {
        return fragment_glsl.status();
    }
    return GlslStages{vertex_glsl.take(), fragment_glsl.take()};
}

} // namespace den::gfx
