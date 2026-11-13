// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/platform/pal.h"
#include "picture_presets.h"

namespace den::player {

namespace {

float Number(const JsonValue& object, std::string_view key, float fallback) {
    const JsonValue* value = object.Find(key);
    return value && value->kind == JsonValue::Kind::Number ? static_cast<float>(value->number) : fallback;
}

bool Flag(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value && value->kind == JsonValue::Kind::Bool && value->boolean;
}

PresetData Plain() {
    PresetData plain;
    plain.info = PicturePreset{"plain", "Plain", "Clean", {}};
    return plain;
}

bool ReadPreset(const JsonValue& json, const std::filesystem::path& dir, PresetData& out) {
    out.info.id = json.String("id");
    out.info.name = json.String("name");
    out.info.group = json.String("group");
    if (out.info.id.empty() || out.info.id == "plain" || out.info.id == PICTURE_AUTO) {
        return false;
    }
    if (out.info.name.empty()) {
        out.info.name = out.info.id;
    }
    out.integer = Flag(json, "integer");
    out.near_integer = json.String("integer") == "near";
    out.input_linear = json.String("input") != "nearest";
    if (const JsonValue* passes = json.Find("passes")) {
        for (const JsonValue& item : passes->items) {
            PresetPass pass;
            pass.shader = item.String("shader");
            if (pass.shader.empty() || !IsRegularFile(dir / (pass.shader + ".glsl"))) {
                log::Warn("player", "picture preset {}: no shader {}", out.info.id, pass.shader);
                return false;
            }
            if (const JsonValue* defines = item.Find("defines")) {
                for (const JsonValue& define : defines->items) {
                    pass.defines.push_back(define.string);
                }
            }
            pass.by_output = item.String("size") == "output";
            pass.scale_x = Number(item, "scale_x", 1.0f);
            pass.scale_y = Number(item, "scale_y", 1.0f);
            pass.linear = item.String("filter") != "nearest";
            pass.feedback = Flag(item, "feedback");
            out.passes.push_back(std::move(pass));
        }
    }
    if (const JsonValue* params = json.Find("params")) {
        for (const JsonValue& item : params->items) {
            PictureParam param;
            param.id = item.String("id");
            if (param.id.empty()) {
                continue;
            }
            param.name = item.String("name").empty() ? param.id : item.String("name");
            param.min = Number(item, "min", 0.0f);
            param.max = std::max(param.min, Number(item, "max", 1.0f));
            param.step = std::max(0.001f, Number(item, "step", 0.05f));
            param.fallback = std::clamp(Number(item, "value", param.min), param.min, param.max);
            param.value = param.fallback;
            if (const JsonValue* labels = item.Find("labels")) {
                for (const JsonValue& label : labels->items) {
                    param.labels.push_back(label.string);
                }
            }
            out.uniforms.emplace_back(param.id, param.fallback);
            out.info.params.push_back(std::move(param));
        }
    }
    if (const JsonValue* fixed = json.Find("fixed")) {
        for (const auto& [id, value] : fixed->members) {
            if (value.kind == JsonValue::Kind::Number) {
                out.uniforms.emplace_back(id, static_cast<float>(value.number));
            }
        }
    }
    return true;
}

} // namespace

Catalogue LoadCatalogue(const std::filesystem::path& shader_dir) {
    Catalogue catalogue;
    catalogue.presets.push_back(Plain());
    const auto text = ReadTextFile(shader_dir / "presets.json");
    const auto json = text ? ParseJson(*text) : std::nullopt;
    if (!json) {
        log::Warn("player", "no picture presets in {}", PathToUtf8(shader_dir));
        return catalogue;
    }
    if (const JsonValue* presets = json->Find("presets")) {
        for (const JsonValue& item : presets->items) {
            PresetData preset;
            const bool known = std::any_of(catalogue.presets.begin(), catalogue.presets.end(),
                                           [&](const PresetData& have) { return have.info.id == item.String("id"); });
            if (!known && ReadPreset(item, shader_dir, preset)) {
                catalogue.presets.push_back(std::move(preset));
            }
        }
    }
    if (const JsonValue* automatic = json->Find("auto")) {
        for (const auto& [system, preset] : automatic->members) {
            catalogue.automatic.emplace_back(system, preset.string);
        }
    }
    return catalogue;
}

std::filesystem::path ShaderDir() {
    return pal::AssetsDir() / "shaders";
}

const Catalogue& Presets() {
    static const Catalogue catalogue = LoadCatalogue(ShaderDir());
    return catalogue;
}

const PresetData* FindPreset(const std::string& id) {
    for (const PresetData& preset : Presets().presets) {
        if (preset.info.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const std::vector<PicturePreset>& PicturePresets() {
    static const std::vector<PicturePreset> list = [] {
        std::vector<PicturePreset> out;
        for (const PresetData& preset : Presets().presets) {
            out.push_back(preset.info);
        }
        return out;
    }();
    return list;
}

std::string AutoPicture(const std::string& system) {
    for (const auto& [id, preset] : Presets().automatic) {
        if (id == system && FindPreset(preset)) {
            return preset;
        }
    }
    return "plain";
}

Result<gfx::EffectSpec> BuildEffect(const PresetData& preset, const std::filesystem::path& shader_dir) {
    gfx::EffectSpec spec;
    const auto common = ReadTextFile(shader_dir / "common.glsl");
    if (!common) {
        return Status::Fail("common.glsl is missing");
    }
    for (const PresetPass& pass : preset.passes) {
        const auto body = ReadTextFile(shader_dir / (pass.shader + ".glsl"));
        if (!body) {
            return Status::Fail(pass.shader + ".glsl is missing");
        }
        gfx::EffectPass out;
        out.name = preset.info.id + "/" + pass.shader;
        for (const std::string& define : pass.defines) {
            out.fragment += "#define " + define + " 1\n";
        }
        out.fragment += *common + "\n" + *body;
        out.size = pass.by_output ? gfx::EffectPass::Size::Output : gfx::EffectPass::Size::Source;
        out.scale_x = pass.scale_x;
        out.scale_y = pass.scale_y;
        out.linear = pass.linear;
        out.feedback = pass.feedback;
        spec.passes.push_back(std::move(out));
    }
    for (const auto& [id, value] : preset.uniforms) {
        (void)value;
        spec.params.push_back(id);
    }
    return spec;
}

} // namespace den::player
