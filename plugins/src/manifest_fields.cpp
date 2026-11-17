// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The manifest's settings, depends and capabilities.

#include <algorithm>
#include <cmath>

#include "json_util.h"
#include "manifest_parts.h"

namespace den::plugins {

namespace {

std::string ScalarText(const JsonValue& value) {
    switch (value.kind) {
    case JsonValue::Kind::String:
        return value.string;
    case JsonValue::Kind::Bool:
        return value.boolean ? "true" : "false";
    case JsonValue::Kind::Number: {
        if (value.number == std::floor(value.number)) {
            return std::to_string(static_cast<long long>(value.number));
        }
        return std::to_string(value.number);
    }
    default:
        return {};
    }
}

bool Has(const std::vector<std::string>& list, std::string_view item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

} // namespace

bool ListsPlatform(const std::vector<std::string>& platforms, std::string_view platform_key) {
    return platforms.empty() || Has(platforms, "any") || Has(platforms, platform_key);
}

bool ManifestSetting::Applies(std::string_view system, std::string_view platform_key,
                              std::string_view core_id) const {
    return (systems.empty() || Has(systems, system)) && ListsPlatform(platforms, platform_key) &&
           (when_core.empty() || when_core == core_id);
}

std::string ManifestSetting::OptionValue(const std::string& value) const {
    for (const auto& [from, to] : option_map) {
        if (from == value) {
            return to;
        }
    }
    return value;
}

void ParseSettings(const JsonValue& settings, Manifest& manifest) {
    for (const JsonValue& value : settings.items) {
        ManifestSetting setting;
        SettingSpec& spec = setting.spec;
        spec.key = value.String("key");
        spec.label = value.String("label");
        spec.type = value.String("type");
        if (spec.key.empty() || spec.label.empty() || spec.type.empty()) {
            continue; // a setting bound to the engine's own has no label here
        }
        spec.help = value.String("help");
        spec.suffix = value.String("suffix");
        spec.section = value.String("section");
        spec.min = json::Number(value, "min", 0);
        spec.max = json::Number(value, "max", 0);
        spec.step = json::Number(value, "step", 1);
        if (const JsonValue* fallback = value.Find("default")) {
            spec.default_value = ScalarText(*fallback);
        }
        if (const JsonValue* choices = value.Find("choices")) {
            for (const JsonValue& choice : choices->items) {
                const JsonValue* choice_value = choice.Find("value");
                spec.choices.emplace_back(choice_value ? ScalarText(*choice_value) : std::string(),
                                          choice.String("label"));
            }
        }
        setting.systems = json::Strings(value, "systems", false);
        setting.platforms = json::Strings(value, "platforms", false);
        setting.when_core = value.String("when_core");
        if (const JsonValue* apply = value.Find("apply")) {
            setting.core_option = apply->String("core_option");
            if (const JsonValue* map = apply->Find("map")) {
                for (const auto& [from, to] : map->members) {
                    setting.option_map.emplace_back(from, ScalarText(to));
                }
            }
        }
        manifest.settings.push_back(std::move(setting));
    }
}

Status ParseDepends(const JsonValue& depends, Manifest& manifest) {
    if (depends.kind != JsonValue::Kind::Array) {
        return Status::Fail("depends is not a list");
    }
    for (const JsonValue& item : depends.items) {
        for (const auto& member : item.members) {
            if (member.first != "id" && member.first != "version") {
                return Status::Fail("a dependency has the unknown field \"" + member.first + "\"");
            }
        }
        Dependency dependency;
        dependency.id = item.String("id");
        if (const std::string range = item.String("version"); !range.empty()) {
            dependency.range = range;
        }
        if (dependency.id.empty() || dependency.id == manifest.id) {
            return Status::Fail("a dependency names no other plugin");
        }
        manifest.depends.push_back(std::move(dependency));
    }
    return {};
}

void ApplyCapabilities(const JsonValue* manifest_level, const JsonValue* system_level, Console& console) {
    // A capability is on unless the system, else the manifest, says false.
    const auto on = [&](const char* name) {
        for (const JsonValue* level : {system_level, manifest_level}) {
            if (const JsonValue* value = level != nullptr ? level->Find(name) : nullptr;
                value != nullptr && value->kind == JsonValue::Kind::Bool) {
                return value->boolean;
            }
        }
        return true;
    };
    console.netplay = on("netplay");
    console.save_states = on("save_states");
    console.rewind = on("rewind");
    console.fast_forward = on("fast_forward");
    console.cheats = on("cheats");
    if (!on("achievements")) {
        console.rcheevos_console = 0;
    }
}

} // namespace den::plugins
