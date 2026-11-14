// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "interface_spec.h"

#include <algorithm>
#include <optional>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/plugins/manifest.h"

namespace den::app {

namespace {

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// "#rrggbb" or "#rrggbbaa".
std::optional<gfx::Color> ParseColor(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') {
        return std::nullopt;
    }
    std::uint8_t parts[4] = {0, 0, 0, 255};
    for (std::size_t i = 1, part = 0; i < text.size(); i += 2, ++part) {
        const int hi = HexDigit(text[i]), lo = HexDigit(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        parts[part] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return gfx::Color{parts[0], parts[1], parts[2], parts[3]};
}

void ReadColor(const JsonValue* object, std::string_view key, gfx::Color& out) {
    if (object != nullptr) {
        if (const auto color = ParseColor(object->String(key))) {
            out = *color;
        }
    }
}

void ReadNumber(const JsonValue* object, std::string_view key, float min, float max, float& out) {
    const JsonValue* value = object != nullptr ? object->Find(key) : nullptr;
    if (value != nullptr && value->kind == JsonValue::Kind::Number) {
        out = std::clamp(static_cast<float>(value->number), min, max);
    }
}

void ReadBool(const JsonValue* object, std::string_view key, bool& out) {
    const JsonValue* value = object != nullptr ? object->Find(key) : nullptr;
    if (value != nullptr && value->kind == JsonValue::Kind::Bool) {
        out = value->boolean;
    }
}

void ReadChoice(const JsonValue* object, std::string_view key, std::initializer_list<const char*> allowed,
                std::string& out) {
    if (object == nullptr) {
        return;
    }
    const std::string value = object->String(key);
    if (std::any_of(allowed.begin(), allowed.end(), [&](const char* a) { return value == a; })) {
        out = value;
    }
}

// A PNG or JPEG of the package as a UTF-8 path; empty for anything else.
std::string PackageImage(const std::filesystem::path& dir, const std::string& relative) {
    const bool image = relative.ends_with(".png") || relative.ends_with(".jpg") || relative.ends_with(".jpeg");
    if (relative.empty() || !image || !plugins::IsSafeRelativePath(relative)) {
        return {};
    }
    return PathToUtf8(dir / PathFromUtf8(relative));
}

} // namespace

bool KnownLayout(std::string_view layout) {
    return layout == "carousel";
}

ConsoleLook InterfaceSpec::LookFor(std::string_view system, std::string_view group) const {
    for (const std::string& key : {std::string(system), "group:" + std::string(group), std::string("*")}) {
        if (const auto it = consoles.find(key); it != consoles.end()) {
            return it->second;
        }
    }
    return {};
}

Result<InterfaceSpec> ParseInterface(std::string_view manifest_json, const std::filesystem::path& dir) {
    const std::optional<JsonValue> root = ParseJson(manifest_json);
    const JsonValue* look = root ? root->Find("interface") : nullptr;
    if (look == nullptr || look->kind != JsonValue::Kind::Object) {
        return Status::Fail("the plugin describes no interface");
    }
    InterfaceSpec spec;
    spec.id = root->String("id");
    spec.name = root->String("name");
    spec.dir = dir;
    if (const std::string layout = look->String("layout"); !layout.empty()) {
        spec.layout = layout;
    }
    if (!KnownLayout(spec.layout)) {
        return Status::Fail("This version of EmuDEN has no \"" + spec.layout + "\" layout.");
    }

    const JsonValue* colors = look->Find("colors");
    ReadColor(colors, "background", spec.background);
    ReadColor(colors, "text", spec.text);
    ReadColor(colors, "dim", spec.dim);
    ReadColor(colors, "accent", spec.accent);
    ReadColor(colors, "panel", spec.panel);

    const JsonValue* backdrop = look->Find("backdrop");
    ReadChoice(backdrop, "art", {"cover", "console", "plain"}, spec.backdrop);
    ReadNumber(backdrop, "tint", 0, 1, spec.tint);
    if (backdrop != nullptr) {
        spec.wallpaper = PackageImage(dir, backdrop->String("wallpaper"));
    }

    const JsonValue* covers = look->Find("covers");
    ReadNumber(covers, "height", 0.3f, 0.9f, spec.cover_height);
    ReadNumber(covers, "side_scale", 0.4f, 1.0f, spec.side_scale);
    ReadNumber(covers, "side_dim", 0, 0.9f, spec.side_dim);
    ReadNumber(covers, "spacing", 0, 120, spec.spacing);
    ReadNumber(covers, "reflection", 0, 0.6f, spec.reflection);
    ReadNumber(covers, "bob", 0, 16, spec.bob);
    ReadNumber(covers, "speed", 4, 40, spec.speed);

    const JsonValue* tabs = look->Find("tabs");
    ReadBool(tabs, "recent", spec.tab_recent);
    ReadBool(tabs, "all", spec.tab_all);

    const JsonValue* launch = look->Find("launch");
    ReadChoice(launch, "animation", {"zoom", "none"}, spec.launch);
    ReadNumber(launch, "seconds", 0.1f, 1.0f, spec.launch_seconds);

    if (const JsonValue* consoles = look->Find("consoles"); consoles != nullptr) {
        for (const auto& [key, value] : consoles->members) {
            if (value.kind != JsonValue::Kind::Object || key.empty() || key.size() > 48) {
                continue;
            }
            ConsoleLook console;
            ReadColor(&value, "color", console.color);
            console.wallpaper = PackageImage(dir, value.String("wallpaper"));
            console.label = value.String("label").substr(0, 16);
            spec.consoles[key] = std::move(console);
        }
    }
    return spec;
}

} // namespace den::app
