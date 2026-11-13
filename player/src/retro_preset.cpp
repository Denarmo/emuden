// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "retro_preset.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>

#include "den/base/files.h"

namespace den::player {

namespace fs = std::filesystem;

namespace {

constexpr int MAX_DEPTH = 16;    // #reference and #include chains
constexpr int MAX_PASSES = 64;
constexpr int MAX_LUTS = 32;

// A key's value and the folder of the file that set it, for the paths in it.
struct Entry {
    std::string value;
    fs::path dir;
};
using Config = std::map<std::string, Entry>;

std::string Unquote(std::string_view text) {
    std::string value = Trim(text);
    if (!value.empty() && value.front() == '"') {
        const std::size_t end = value.find('"', 1);
        return value.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    }
    // An unquoted value ends where a comment starts.
    const std::size_t hash = value.find('#');
    return Trim(value.substr(0, hash));
}

Status ReadConfig(const fs::path& file, int depth, Config& config) {
    if (depth > MAX_DEPTH) {
        return Status::Fail("the presets refer to each other too deeply");
    }
    const std::optional<std::string> text = ReadTextFile(file);
    if (!text) {
        return Status::Fail("cannot read " + PathToUtf8(file.filename()));
    }
    const fs::path dir = file.parent_path();
    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.rfind("#reference", 0) == 0) {
            // What it builds on comes first; this file's own keys then win.
            const fs::path base = (dir / PathFromUtf8(Unquote(trimmed.substr(10)))).lexically_normal();
            if (Status read = ReadConfig(base, depth + 1, config); !read.ok) {
                return read;
            }
            continue;
        }
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.rfind("//", 0) == 0) {
            continue;
        }
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = Trim(trimmed.substr(0, equals));
        if (!key.empty()) {
            config[key] = Entry{Unquote(trimmed.substr(equals + 1)), dir};
        }
    }
    return Status{};
}

const Entry* Find(const Config& config, const std::string& key) {
    const auto it = config.find(key);
    return it == config.end() ? nullptr : &it->second;
}

bool Flag(const Config& config, const std::string& key, bool fallback = false) {
    const Entry* entry = Find(config, key);
    if (!entry) {
        return fallback;
    }
    const std::string value = ToLower(entry->value);
    return value == "true" || value == "1" || value == "yes";
}

std::optional<float> Number(const Config& config, const std::string& key) {
    const Entry* entry = Find(config, key);
    if (!entry || entry->value.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const float value = std::strtof(entry->value.c_str(), &end);
    if (end == entry->value.c_str()) {
        return std::nullopt;
    }
    return value;
}

fs::path PathOf(const Entry& entry) {
    return (entry.dir / PathFromUtf8(entry.value)).lexically_normal();
}

gfx::RetroWrap WrapOf(const Config& config, const std::string& key) {
    const Entry* entry = Find(config, key);
    const std::string value = entry ? ToLower(entry->value) : std::string();
    if (value == "clamp_to_edge") {
        return gfx::RetroWrap::Edge;
    }
    if (value == "repeat") {
        return gfx::RetroWrap::Repeat;
    }
    if (value == "mirrored_repeat") {
        return gfx::RetroWrap::Mirror;
    }
    return gfx::RetroWrap::Border;
}

std::optional<gfx::RetroScale> ScaleOf(const Config& config, const std::string& key) {
    const Entry* entry = Find(config, key);
    if (!entry) {
        return std::nullopt;
    }
    const std::string value = ToLower(entry->value);
    if (value == "viewport") {
        return gfx::RetroScale::Viewport;
    }
    if (value == "absolute") {
        return gfx::RetroScale::Absolute;
    }
    return gfx::RetroScale::Source;
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> items;
    std::string item;
    std::istringstream in(text);
    while (std::getline(in, item, ';')) {
        if (std::string name = Trim(item); !name.empty()) {
            items.push_back(std::move(name));
        }
    }
    return items;
}

Status Expand(const fs::path& file, int depth, std::string& out) {
    if (depth > MAX_DEPTH) {
        return Status::Fail("the shaders include each other too deeply");
    }
    const std::optional<std::string> text = ReadTextFile(file);
    if (!text) {
        return Status::Fail("cannot read " + PathToUtf8(file.filename()));
    }
    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string trimmed = Trim(line);
        if (trimmed.rfind("#include", 0) == 0) {
            const fs::path included = (file.parent_path() / PathFromUtf8(Unquote(trimmed.substr(8)))).lexically_normal();
            if (Status read = Expand(included, depth + 1, out); !read.ok) {
                return read;
            }
            continue;
        }
        out += line;
        out += '\n';
    }
    return Status{};
}

} // namespace

bool IsRetroPreset(const fs::path& file) {
    const std::string extension = ToLower(PathToUtf8(file.extension()));
    return extension == ".slangp" || extension == ".glslp";
}

bool ParseRetroParam(std::string_view line, gfx::RetroParam& out) {
    std::string text = Trim(line);
    if (text.rfind("#pragma", 0) != 0) {
        return false;
    }
    text = Trim(text.substr(7));
    if (text.rfind("parameter", 0) != 0) {
        return false;
    }
    text = Trim(text.substr(9));
    const std::size_t space = text.find_first_of(" \t");
    if (space == std::string::npos) {
        return false;
    }
    gfx::RetroParam param;
    param.id = text.substr(0, space);
    text = Trim(text.substr(space));
    if (!text.empty() && text.front() == '"') {
        const std::size_t end = text.find('"', 1);
        if (end == std::string::npos) {
            return false;
        }
        param.description = text.substr(1, end - 1);
        text = text.substr(end + 1);
    }
    float numbers[4] = {0, 0, 0, 0};
    int count = 0;
    const char* at = text.c_str();
    while (count < 4) {
        char* end = nullptr;
        const float value = std::strtof(at, &end);
        if (end == at) {
            break;
        }
        numbers[count++] = value;
        at = end;
    }
    if (count < 3) {
        return false;
    }
    param.initial = numbers[0];
    param.minimum = numbers[1];
    param.maximum = std::max(numbers[1], numbers[2]);
    param.step = count == 4 && numbers[3] > 0 ? numbers[3] : (param.maximum - param.minimum) / 100.0f;
    if (param.step <= 0) {
        param.step = 0.01f;
    }
    out = std::move(param);
    return true;
}

Result<RetroShaderSource> LoadRetroShader(const fs::path& file, gfx::RetroLanguage language) {
    std::string text;
    if (Status read = Expand(file, 0, text); !read.ok) {
        return read;
    }
    RetroShaderSource source;
    enum class Stage { Both, Vertex, Fragment } stage = Stage::Both;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.rfind("#pragma", 0) == 0) {
            const std::string pragma = Trim(trimmed.substr(7));
            gfx::RetroParam param;
            if (ParseRetroParam(trimmed, param)) {
                source.params.push_back(std::move(param));
                continue;
            }
            if (pragma.rfind("name", 0) == 0) {
                source.alias = Trim(pragma.substr(4));
                continue;
            }
            if (pragma.rfind("format", 0) == 0) {
                source.format = Trim(pragma.substr(6));
                continue;
            }
            if (pragma.rfind("stage", 0) == 0 && language == gfx::RetroLanguage::Slang) {
                const std::string which = Trim(pragma.substr(5));
                stage = which == "vertex" ? Stage::Vertex : which == "fragment" ? Stage::Fragment : Stage::Both;
                continue;
            }
        }
        if (stage != Stage::Fragment) {
            source.vertex += line + '\n';
        }
        if (stage != Stage::Vertex) {
            source.fragment += line + '\n';
        }
    }
    return source;
}

Result<gfx::RetroSpec> LoadRetroPreset(const fs::path& file, bool images) {
    Config config;
    if (Status read = ReadConfig(file, 0, config); !read.ok) {
        return read;
    }
    gfx::RetroSpec spec;
    spec.language = ToLower(PathToUtf8(file.extension())) == ".glslp" ? gfx::RetroLanguage::Glsl
                                                                       : gfx::RetroLanguage::Slang;
    const int count = static_cast<int>(Number(config, "shaders").value_or(0));
    if (count <= 0 || count > MAX_PASSES) {
        return Result<gfx::RetroSpec>::Fail("the preset has no shaders");
    }
    for (int i = 0; i < count; ++i) {
        const std::string n = std::to_string(i);
        const Entry* shader = Find(config, "shader" + n);
        if (!shader) {
            return Result<gfx::RetroSpec>::Fail("the preset has no shader" + n);
        }
        Result<RetroShaderSource> source = LoadRetroShader(PathOf(*shader), spec.language);
        if (!source) {
            return source.status();
        }
        gfx::RetroPassSpec pass;
        pass.name = PathToUtf8(PathOf(*shader).filename());
        pass.vertex = std::move(source->vertex);
        pass.fragment = std::move(source->fragment);
        const Entry* alias = Find(config, "alias" + n);
        pass.alias = alias ? alias->value : source->alias;
        pass.linear = Flag(config, "filter_linear" + n);
        pass.wrap = WrapOf(config, "wrap_mode" + n);
        pass.mipmap_input = Flag(config, "mipmap_input" + n);
        // A Slang shader names its target's format; the preset's flags are the older way.
        const std::string& format = source->format;
        const bool sfloat = format.size() > 6 && format.compare(format.size() - 6, 6, "SFLOAT") == 0;
        pass.float_target = Flag(config, "float_framebuffer" + n) || sfloat;
        pass.float32 = sfloat && format.find("32") != std::string::npos;
        pass.srgb_target = Flag(config, "srgb_framebuffer" + n) || format.find("_SRGB") != std::string::npos;
        const auto both = ScaleOf(config, "scale_type" + n);
        const auto x = ScaleOf(config, "scale_type_x" + n);
        const auto y = ScaleOf(config, "scale_type_y" + n);
        pass.scale_given = both || x || y;
        pass.scale_x = x.value_or(both.value_or(gfx::RetroScale::Source));
        pass.scale_y = y.value_or(both.value_or(gfx::RetroScale::Source));
        const float factor = Number(config, "scale" + n).value_or(1.0f);
        pass.factor_x = Number(config, "scale_x" + n).value_or(factor);
        pass.factor_y = Number(config, "scale_y" + n).value_or(factor);
        pass.frame_count_mod = static_cast<unsigned>(std::max(0.0f, Number(config, "frame_count_mod" + n).value_or(0)));
        for (gfx::RetroParam& param : source->params) {
            const bool known = std::any_of(spec.params.begin(), spec.params.end(),
                                           [&](const gfx::RetroParam& seen) { return seen.id == param.id; });
            if (!known) {
                spec.params.push_back(std::move(param));
            }
        }
        spec.passes.push_back(std::move(pass));
    }
    if (const Entry* textures = Find(config, "textures")) {
        for (const std::string& name : SplitList(textures->value)) {
            const Entry* path = Find(config, name);
            if (!path || static_cast<int>(spec.luts.size()) >= MAX_LUTS) {
                continue;
            }
            gfx::RetroLut lut;
            lut.name = name;
            lut.linear = Flag(config, name + "_linear");
            lut.wrap = WrapOf(config, name + "_wrap_mode");
            lut.mipmap = Flag(config, name + "_mipmap");
            if (images) {
                Result<gfx::ImageData> image = gfx::LoadImageFile(PathOf(*path));
                if (!image) {
                    return Result<gfx::RetroSpec>::Fail("cannot read the texture " + PathToUtf8(PathOf(*path).filename()));
                }
                lut.image = image.take();
            }
            spec.luts.push_back(std::move(lut));
        }
    }
    // A preset sets a slider by its id, listed under "parameters" or not.
    for (const gfx::RetroParam& param : spec.params) {
        const float value = Number(config, param.id).value_or(param.initial);
        spec.values.push_back(std::clamp(value, param.minimum, param.maximum));
    }
    return spec;
}

} // namespace den::player
