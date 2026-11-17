// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "interface_spec.h"

#include <algorithm>
#include <cmath>
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

void ReadInt(const JsonValue* object, std::string_view key, int min, int max, int& out) {
    float value = static_cast<float>(out);
    ReadNumber(object, key, static_cast<float>(min), static_cast<float>(max), value);
    out = static_cast<int>(std::lround(value));
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

void ReadObject(const JsonValue* object, const std::vector<std::string>& moulds, ObjectLook& out) {
    if (object == nullptr || object->kind != JsonValue::Kind::Object) {
        return;
    }
    ReadChoice(object, "shape", {"cartridge", "card", "case", "jewel", "disc"}, out.shape);
    if (const std::string style = object->String("style");
        style == "plain" || style == "pocket" || style == "pocket_wide" ||
        std::find(moulds.begin(), moulds.end(), style) != moulds.end()) {
        out.style = style;
    }
    ReadChoice(object, "label_edge", {"none", "paper", "foil"}, out.label_edge);
    ReadBool(object, "box", out.box);
    ReadBool(object, "umd", out.umd);
    ReadNumber(object, "box_aspect", 0.4f, 2.0f, out.box_aspect);
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

std::optional<gfx::Color> ParseHexColor(std::string_view text) {
    return ParseColor(text);
}

bool KnownLayout(std::string_view layout) {
    return layout == "carousel" || layout == "shelf3d" || layout == "grid" || layout == "list" ||
           layout == "wheel" || layout == "hero";
}

ConsoleLook InterfaceSpec::LookFor(std::string_view system, std::string_view group) const {
    for (const std::string& key : {std::string(system), "group:" + std::string(group), std::string("*")}) {
        if (const auto it = consoles.find(key); it != consoles.end()) {
            return it->second;
        }
    }
    return {};
}

namespace {

constexpr std::size_t MAX_LOOKS = 16;

bool PlainLookId(std::string_view id) {
    return !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

Result<InterfaceSpec> ReadLook(const JsonValue& manifest, const JsonValue* look, const std::filesystem::path& dir,
                               const std::vector<std::string>& moulds) {
    if (look == nullptr || look->kind != JsonValue::Kind::Object) {
        return Status::Fail("the plugin describes no interface");
    }
    InterfaceSpec spec;
    spec.id = manifest.String("id");
    spec.name = manifest.String("name");
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

    const JsonValue* grid = look->Find("grid");
    ReadInt(grid, "columns", 0, 12, spec.grid.columns);
    spec.grid.columns = spec.grid.columns == 1 ? 2 : spec.grid.columns;
    ReadBool(grid, "panel", spec.grid.panel);
    ReadBool(grid, "titles", spec.grid.titles);

    const JsonValue* list = look->Find("list");
    ReadInt(list, "rows", 5, 16, spec.list.rows);
    ReadBool(list, "art", spec.list.art);
    std::string side = spec.list.art_left ? "left" : "right";
    ReadChoice(list, "art_side", {"left", "right"}, side);
    spec.list.art_left = side == "left";
    ReadBool(list, "thumbnails", spec.list.thumbnails);

    const JsonValue* wheel = look->Find("wheel");
    side = spec.wheel.left ? "left" : "right";
    ReadChoice(wheel, "side", {"left", "right"}, side);
    spec.wheel.left = side == "left";
    ReadInt(wheel, "names", 5, 15, spec.wheel.names);
    spec.wheel.names |= 1; // odd: the selected name in the middle
    ReadNumber(wheel, "curve", 0, 1, spec.wheel.curve);

    ReadNumber(look->Find("hero"), "strip", 0.14f, 0.4f, spec.hero.strip);

    const JsonValue* tabs = look->Find("tabs");
    ReadBool(tabs, "continue", spec.tab_continue);
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
            ReadObject(value.Find("object"), moulds, console.object);
            spec.consoles[key] = std::move(console);
        }
    }
    return spec;
}

} // namespace

std::string InterfaceSpec::Key() const {
    return look_id.empty() ? id : id + "/" + look_id;
}

Result<InterfaceSpec> ParseInterface(std::string_view manifest_json, const std::filesystem::path& dir,
                                     const std::vector<std::string>& moulds) {
    const std::optional<JsonValue> root = ParseJson(manifest_json);
    if (!root) {
        return Status::Fail("the plugin describes no interface");
    }
    return ReadLook(*root, root->Find("interface"), dir, moulds);
}

Result<std::vector<InterfaceSpec>> ParseInterfaces(std::string_view manifest_json, const std::filesystem::path& dir,
                                                   const std::vector<std::string>& moulds) {
    const std::optional<JsonValue> root = ParseJson(manifest_json);
    const JsonValue* looks = root ? root->Find("interfaces") : nullptr;
    if (looks == nullptr || looks->kind != JsonValue::Kind::Array) {
        Result<InterfaceSpec> one = ParseInterface(manifest_json, dir, moulds);
        if (!one) {
            return Status::Fail(one.error());
        }
        return std::vector<InterfaceSpec>{one.take()};
    }
    std::vector<InterfaceSpec> out;
    for (const JsonValue& entry : looks->items) {
        Result<InterfaceSpec> spec = ReadLook(*root, &entry, dir, moulds);
        if (!spec || out.size() >= MAX_LOOKS) {
            continue;
        }
        const std::string id = entry.String("id");
        spec->look_id = PlainLookId(id) ? id : spec->layout;
        if (const std::string name = entry.String("name").substr(0, 40); !name.empty()) {
            spec->name = name;
        }
        const bool taken = std::any_of(out.begin(), out.end(),
                                       [&](const InterfaceSpec& s) { return s.look_id == spec->look_id; });
        if (!taken) {
            out.push_back(spec.take());
        }
    }
    if (out.empty()) {
        return Status::Fail("This version of EmuDEN has none of the plugin's Home looks.");
    }
    return out;
}

} // namespace den::app
