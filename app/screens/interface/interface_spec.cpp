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

// [x, y, w, h] within 0..1.
void ReadUnitRect(const JsonValue* rect, gfx::Rect& out) {
    if (rect == nullptr || rect->kind != JsonValue::Kind::Array || rect->items.size() != 4) {
        return;
    }
    float v[4];
    for (int i = 0; i < 4; ++i) {
        const JsonValue& item = rect->items[static_cast<std::size_t>(i)];
        v[i] = item.kind == JsonValue::Kind::Number ? std::clamp(static_cast<float>(item.number), 0.0f, 1.0f) : 0;
    }
    if (v[2] > 0.05f && v[3] > 0.05f && v[0] + v[2] <= 1.001f && v[1] + v[3] <= 1.001f) {
        out = gfx::Rect{v[0], v[1], v[2], v[3]};
    }
}

void ReadObject(const JsonValue* object, ObjectLook& out) {
    if (object == nullptr || object->kind != JsonValue::Kind::Object) {
        return;
    }
    ReadChoice(object, "shape", {"cartridge", "card", "case", "jewel", "disc"}, out.shape);
    ReadChoice(object, "style", {"plain", "pocket", "pocket_wide"}, out.style);
    ReadChoice(object, "label_edge", {"none", "paper", "foil"}, out.label_edge);
    ReadBool(object, "box", out.box);
    ReadNumber(object, "box_aspect", 0.4f, 2.0f, out.box_aspect);
    ReadChoice(object, "player", {"portrait", "landscape", "clamshell", "home"}, out.player);
    if (const auto color = ParseColor(object->String("player_color"))) {
        out.player_color = *color;
        out.player_color.a = 255;
    }
    if (const JsonValue* colors = object->Find("player_colors"); colors && colors->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : colors->items) {
            if (const auto color = ParseColor(item.string); color && out.player_colors.size() < 16) {
                out.player_colors.push_back(gfx::Color{color->r, color->g, color->b, 255});
            }
        }
    }
    if (const std::string photo = object->String("player_photo");
        photo == "launchbox" || (photo.starts_with("https://") && photo.size() < 512)) {
        out.player_photo = photo;
    }
    ReadUnitRect(object->Find("player_screen"), out.player_screen);
    // A console modelled on the real one: it takes the generic one's place, and its photograph's.
    if (std::string model; ReadChoice(object, "player_model", {"dmg", "cgb", "agb", "ds_lite", "switch2"}, model),
        !model.empty()) {
        out.player = model;
        out.player_photo.clear();
    }
    ReadNumber(object, "aspect", 0.3f, 2.5f, out.aspect);
    ReadNumber(object, "depth", 0.01f, 0.6f, out.depth);
    if (const auto shell = ParseColor(object->String("shell"))) {
        out.shell = *shell;
        out.has_shell = true;
    }
    ReadBool(object, "disc", out.disc);
    ReadNumber(object, "radius", 0, 0.45f, out.radius);
    ReadNumber(object, "notch", 0, 0.4f, out.notch);
    float grip = static_cast<float>(out.grip);
    ReadNumber(object, "grip", 0, 8, grip);
    out.grip = static_cast<int>(grip);
    ReadUnitRect(object->Find("label"), out.label);
}

} // namespace

bool KnownLayout(std::string_view layout) {
    return layout == "carousel" || layout == "shelf3d";
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

    const JsonValue* objects = look->Find("objects");
    float side_degrees = 52, sway_degrees = 7;
    ReadNumber(objects, "side_angle", 0, 80, side_degrees);
    ReadNumber(objects, "sway", 0, 30, sway_degrees);
    spec.side_angle = side_degrees * 0.0174533f;
    spec.sway = sway_degrees * 0.0174533f;

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
            ReadObject(value.Find("object"), console.object);
            spec.consoles[key] = std::move(console);
        }
    }
    return spec;
}

} // namespace den::app
