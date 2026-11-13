// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// RetroArch presets the user imported, under <data>/shaders: every .slangp and .glslp
// there becomes a picture preset of the "Imported" group, its sliders from the shaders'
// "#pragma parameter" lines.

#include <algorithm>
#include <cctype>
#include <memory>
#include <system_error>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/platform/pal.h"
#include "picture_presets.h"
#include "retro_preset.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_PRESETS = 400;

struct Imported {
    std::vector<std::unique_ptr<PresetData>> presets;
    // Lists replaced by a scan stay alive: a session may still point into them.
    std::vector<std::vector<std::unique_ptr<PresetData>>> retired;
    bool scanned = false;
    unsigned generation = 0;
};

Imported& Store() {
    static Imported state;
    return state;
}

std::string IdOf(const fs::path& relative) {
    std::string id = "custom-";
    bool dash = false;
    for (const char c : PathToUtf8(relative)) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            dash = false;
        } else if (!dash) {
            id += '-';
            dash = true;
        }
    }
    return id;
}

std::string NameOf(const fs::path& file) {
    std::string name = PathToUtf8(file.stem());
    std::replace(name.begin(), name.end(), '_', ' ');
    std::replace(name.begin(), name.end(), '-', ' ');
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name;
}

std::unique_ptr<PresetData> Read(const fs::path& root, const fs::path& file) {
    const Result<gfx::RetroSpec> spec = LoadRetroPreset(file, false);
    if (!spec) {
        log::Warn("player", "imported shader {}: {}", PathToUtf8(file.filename()), spec.error());
        return nullptr;
    }
    std::error_code error;
    const fs::path relative = fs::relative(file, root, error);
    auto preset = std::make_unique<PresetData>();
    preset->retro = file;
    preset->input_linear = false;
    preset->info.id = IdOf(error ? file.filename() : relative);
    preset->info.name = NameOf(file);
    preset->info.group = "Imported";
    for (std::size_t i = 0; i < spec.value().params.size(); ++i) {
        const gfx::RetroParam& from = spec.value().params[i];
        PictureParam param;
        param.id = from.id;
        param.name = from.description.empty() ? from.id : from.description;
        param.min = std::min(from.minimum, from.maximum);
        param.max = std::max(from.minimum, from.maximum);
        param.step = std::max(0.001f, from.step);
        const float value = i < spec.value().values.size() ? spec.value().values[i] : from.initial;
        param.fallback = std::clamp(value, param.min, param.max);
        param.value = param.fallback;
        preset->uniforms.emplace_back(param.id, param.fallback);
        preset->info.params.push_back(std::move(param));
    }
    return preset;
}

void Scan() {
    Imported& state = Store();
    state.scanned = true;
    std::vector<std::unique_ptr<PresetData>> found;
    const fs::path root = ImportedShaderDir();
    std::error_code error;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && IsRetroPreset(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.size() > MAX_PRESETS) {
        log::Warn("player", "{} imported shader presets, only the first {} are listed", files.size(), MAX_PRESETS);
        files.resize(MAX_PRESETS);
    }
    for (const fs::path& file : files) {
        auto preset = Read(root, file);
        const bool taken = preset && std::any_of(found.begin(), found.end(), [&](const auto& have) {
                               return have->info.id == preset->info.id;
                           });
        if (preset && !taken) {
            found.push_back(std::move(preset));
        }
    }
    // A repository often has a Slang and a GLSL copy of each preset: the GLSL one says so.
    for (auto& preset : found) {
        const bool twin = std::any_of(found.begin(), found.end(), [&](const auto& other) {
            return other != preset && other->info.name == preset->info.name;
        });
        if (twin && ToLower(PathToUtf8(preset->retro.extension())) == ".glslp") {
            preset->info.name += " (GLSL)";
        }
    }
    state.retired.push_back(std::move(state.presets));
    state.presets = std::move(found);
    ++state.generation;
}

} // namespace

fs::path ImportedShaderDir() {
    return pal::DataDir() / "shaders";
}

void RefreshImportedPresets() {
    Scan();
}

const std::vector<std::unique_ptr<PresetData>>& ImportedPresets() {
    if (!Store().scanned) {
        Scan();
    }
    return Store().presets;
}

unsigned ImportedGeneration() {
    ImportedPresets();
    return Store().generation;
}

} // namespace den::player
