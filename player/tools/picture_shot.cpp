// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Draws a still picture through a picture preset at any display size, without a screen of
// that size: the chain renders into a framebuffer of its own, which is saved as a PNG.
//   den_picture_shot --image <frame.png> --preset <id> --size <w>x<h> --out <png>
//       [--shaders <folder>] [--aspect <w/h>] [--fill] [--param <id>=<value>]...
//       [--frames <n>] [--bench <n>]
//   den_picture_shot --image <frame.png> --retro <file.slangp|.glslp> --size ... (the same)
// The picture is placed the way the player places it (picture_fit.h); --fill turns the
// preset's wish for a whole multiple off. --bench prints the GPU time of a frame.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/effect_chain.h"
#include "den/gfx/gl.h"
#include "den/gfx/retro_chain.h"
#include "den/gfx/images.h"
#include "den/platform/pal.h"
#include "picture_fit.h"
#include "picture_presets.h"
#include "retro_preset.h"

using namespace den;

namespace {

struct Args {
    std::filesystem::path image, out, shaders, retro;
    std::string preset = "dotmatrix";
    int width = 1280, height = 720;
    float aspect = 0;
    bool fill = false;
    int frames = 8, bench = 0;
    std::vector<std::pair<std::string, float>> params;
};

Args Parse(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (flag == "--image") {
            args.image = PathFromUtf8(value());
        } else if (flag == "--out") {
            args.out = PathFromUtf8(value());
        } else if (flag == "--shaders") {
            args.shaders = PathFromUtf8(value());
        } else if (flag == "--retro") {
            args.retro = PathFromUtf8(value());
        } else if (flag == "--preset") {
            args.preset = value();
        } else if (flag == "--size") {
            const std::string size = value();
            const std::size_t at = size.find("x");
            if (at != std::string::npos) {
                args.width = std::atoi(size.c_str());
                args.height = std::atoi(size.c_str() + at + 1);
            }
        } else if (flag == "--aspect") {
            args.aspect = static_cast<float>(std::atof(value().c_str()));
        } else if (flag == "--fill") {
            args.fill = true;
        } else if (flag == "--frames") {
            args.frames = std::max(1, std::atoi(value().c_str()));
        } else if (flag == "--bench") {
            args.bench = std::atoi(value().c_str());
        } else if (flag == "--param") {
            const std::string param = value();
            const std::size_t at = param.find("=");
            if (at != std::string::npos) {
                args.params.emplace_back(param.substr(0, at),
                                         static_cast<float>(std::atof(param.c_str() + at + 1)));
            }
        }
    }
    return args;
}

int Shoot(const Args& args) {
    pal::WindowDesc desc;
    desc.title = "EmuDEN picture shot";
    desc.width = 320;
    desc.height = 240;
    desc.hidden = true;
    auto window = pal::CreateAppWindow(desc);
    if (!window) {
        log::Error("shot", "{}", window.error());
        return 1;
    }
    (*window)->SetVsync(false);
    gfx::Device device;
    if (const Status ready = device.Init(pal::GlProcAddress, pal::GetCaps().gles); !ready) {
        log::Error("shot", "{}", ready.error);
        return 1;
    }
    const std::filesystem::path dir = args.shaders.empty() ? player::ShaderDir() : args.shaders;
    player::PresetData preset;
    gfx::EffectChain chain;
    gfx::RetroChain retro;
    if (!args.retro.empty()) {
        auto spec = player::LoadRetroPreset(args.retro);
        if (!spec) {
            log::Error("shot", "{}", spec.error());
            return 2;
        }
        if (const Status loaded = retro.Load(device, spec.value()); !loaded) {
            log::Error("shot", "{}", loaded.error);
            return 3;
        }
        preset.input_linear = false;
        for (std::size_t i = 0; i < spec.value().params.size(); ++i) {
            preset.uniforms.emplace_back(spec.value().params[i].id, spec.value().values[i]);
        }
    } else {
        const player::Catalogue catalogue = player::LoadCatalogue(dir);
        const auto found = std::find_if(catalogue.presets.begin(), catalogue.presets.end(),
                                        [&](const player::PresetData& p) { return p.info.id == args.preset; });
        if (found == catalogue.presets.end() || found->passes.empty()) {
            log::Error("shot", "no preset {} with passes in {}", args.preset, PathToUtf8(dir));
            return 2;
        }
        preset = *found;
        const auto spec = player::BuildEffect(preset, dir);
        if (!spec || !chain.Load(device, spec.value())) {
            log::Error("shot", "the preset does not compile");
            return 3;
        }
    }
    const auto image = gfx::LoadImageFile(args.image);
    if (!image) {
        log::Error("shot", "{}", image.error());
        return 2;
    }
    for (std::size_t i = 0; i < preset.uniforms.size(); ++i) {
        float value = preset.uniforms[i].second;
        for (const auto& [id, given] : args.params) {
            if (id == preset.uniforms[i].first) {
                value = given;
            }
        }
        chain.SetParam(i, value);
        retro.SetParam(i, value);
    }

    const gfx::ImageData& frame = image.value();
    gfx::Texture source = device.CreateTexture(frame.width, frame.height, gfx::PixelFormat::Rgba8,
                                               frame.rgba.data(), preset.input_linear);
    gfx::Texture target =
        device.CreateTexture(args.width, args.height, gfx::PixelFormat::Rgba8, nullptr, false);
    unsigned framebuffer = 0;
    gl::GenFramebuffers(1, &framebuffer);
    gl::BindFramebuffer(gl::FRAMEBUFFER, framebuffer);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, target.id, 0);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT);

    const float aspect =
        args.aspect > 0 ? args.aspect : static_cast<float>(frame.width) / static_cast<float>(frame.height);
    player::WholeScale whole = player::WholeScale::Off;
    if (preset.integer) {
        whole = player::WholeScale::Always;
    } else if (preset.near_integer && !args.fill) {
        whole = player::WholeScale::Near;
    }
    const player::PictureSize fit = player::FitPicture(
        static_cast<float>(args.width), static_cast<float>(args.height), aspect, frame.height, whole);
    gfx::EffectChain::Input input;
    input.texture = source.id;
    input.width = static_cast<float>(frame.width);
    input.height = static_cast<float>(frame.height);
    gfx::EffectChain::Output output;
    output.framebuffer = framebuffer;
    output.target = gfx::Rect{std::round((static_cast<float>(args.width) - fit.w) * 0.5f),
                              std::round((static_cast<float>(args.height) - fit.h) * 0.5f), std::round(fit.w),
                              std::round(fit.h)};
    output.view_width = args.width;
    output.view_height = args.height;
    log::Info("shot", "{} at {}x{}: picture {:.0f}x{:.0f}, {:.3f} pixels per row", args.preset, args.width,
              args.height, output.target.w, output.target.h, output.target.h / input.height);

    std::uint64_t serial = 0;
    const auto draw = [&] {
        input.serial = ++serial;
        input.frame = serial;
        if (retro.Loaded()) {
            retro.Draw(device, input, output);
        } else {
            chain.Draw(device, input, output);
        }
    };
    for (int i = 0; i < args.frames; ++i) {
        draw();
    }
    if (!args.out.empty()) {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(args.width) * args.height * 4);
        gl::BindFramebuffer(gl::FRAMEBUFFER, framebuffer);
        gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, args.width, args.height, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
        const std::size_t row = static_cast<std::size_t>(args.width) * 4;
        std::vector<std::uint8_t> flipped(pixels.size());
        for (int y = 0; y < args.height; ++y) {
            std::memcpy(flipped.data() + row * y, pixels.data() + row * (args.height - 1 - y), row);
        }
        const Status saved = gfx::SavePng(args.out, args.width, args.height, flipped.data());
        log::Info("shot", "{}", saved ? PathToUtf8(args.out) : saved.error);
    }
    if (args.bench > 0) {
        gl::Finish();
        const double start = pal::NowSeconds();
        for (int i = 0; i < args.bench; ++i) {
            draw();
        }
        gl::Finish();
        const double each = (pal::NowSeconds() - start) * 1000.0 / args.bench;
        log::Info("shot", "bench: {:.3f} ms a frame ({:.0f} fps) at {}x{}", each, 1000.0 / each, args.width,
                  args.height);
    }
    chain.Release();
    retro.Release();
    gl::DeleteFramebuffers(1, &framebuffer);
    device.DestroyTexture(target);
    device.DestroyTexture(source);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const Args args = Parse(argc, argv);
    if (args.image.empty()) {
        log::Error("shot", "usage: den_picture_shot --image <png> --preset <id> --size <w>x<h> --out <png>");
        return 1;
    }
    if (const Status started = pal::Init(); !started) {
        log::Error("shot", "{}", started.error);
        return 1;
    }
    const int code = Shoot(args);
    pal::Shutdown();
    return code;
}
