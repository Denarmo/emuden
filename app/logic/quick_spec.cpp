// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "quick_spec.h"

#include <algorithm>
#include <cmath>

#include "den/base/json.h"
#include "interface_spec.h"

namespace den::app {

namespace {

constexpr std::size_t MAX_CHOICES = 16;

bool Has(const std::vector<std::string>& list, std::string_view item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

bool PlainId(std::string_view id) {
    return !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string Title(std::string_view layout) {
    std::string out(layout);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

void ReadColor(const JsonValue* colors, std::string_view key, std::optional<gfx::Color>& out) {
    if (colors != nullptr && colors->kind == JsonValue::Kind::Object) {
        if (const auto color = ParseHexColor(colors->String(key))) {
            out = color;
        }
    }
}

// Known action ids of a list, each once.
std::vector<std::string> ReadIds(const JsonValue* list) {
    std::vector<std::string> out;
    if (list == nullptr || list->kind != JsonValue::Kind::Array) {
        return out;
    }
    for (const JsonValue& item : list->items) {
        if (item.kind == JsonValue::Kind::String && KnownQuickAction(item.string) && !Has(out, item.string)) {
            out.push_back(item.string);
        }
    }
    return out;
}

float Number(const JsonValue& object, std::string_view key, float min, float max, float fallback) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->kind != JsonValue::Kind::Number || !std::isfinite(value->number)) {
        return fallback;
    }
    return std::clamp(static_cast<float>(value->number), min, max);
}

std::optional<QuickSpec> ReadChoice(const JsonValue& value, const std::string& plugin_id) {
    if (value.kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    QuickSpec spec;
    spec.plugin_id = plugin_id;
    if (const std::string layout = value.String("layout"); !layout.empty()) {
        spec.layout = layout;
    }
    if (!KnownQuickLayout(spec.layout)) {
        return std::nullopt;
    }
    const std::string id = value.String("id");
    spec.choice_id = PlainId(id) ? id : spec.layout;
    spec.name = value.String("name").substr(0, 40);
    if (spec.name.empty()) {
        spec.name = Title(spec.layout);
    }
    const JsonValue* colors = value.Find("colors");
    ReadColor(colors, "background", spec.background);
    ReadColor(colors, "text", spec.text);
    ReadColor(colors, "dim", spec.dim);
    ReadColor(colors, "panel", spec.panel);
    ReadColor(colors, "accent", spec.accent);
    spec.order = ReadIds(value.Find("actions"));
    spec.hidden = ReadIds(value.Find("hidden"));
    spec.tile_size = Number(value, "tile_size", 0.75f, 1.5f, 1.0f);
    const int columns = static_cast<int>(Number(value, "columns", 0, 6, 0));
    spec.columns = columns < 2 ? 0 : columns;
    spec.dock = static_cast<int>(Number(value, "dock", 3, 7, 5));
    if (const JsonValue* cover = value.Find("cover"); cover != nullptr && cover->kind == JsonValue::Kind::Bool) {
        spec.cover = cover->boolean;
    }
    return spec;
}

} // namespace

const std::vector<std::string>& QuickActionIds() {
    static const std::vector<std::string> ids = {
        "pause", "rewind", "fast_forward", "exit", "states",  "screen",  "swap",   "fps",
        "bfi",   "controls", "options",    "buttons", "cheats", "apps",  "volume",
    };
    return ids;
}

bool KnownQuickAction(std::string_view id) {
    return Has(QuickActionIds(), id);
}

bool KnownQuickLayout(std::string_view layout) {
    return layout == "standard" || layout == "grid" || layout == "compact" || layout == "dock" ||
           layout == "split" || layout == "performance";
}

const std::vector<std::string>& DefaultQuickActions(std::string_view layout) {
    // Today's page: the controls of the card, then the sections.
    static const std::vector<std::string> standard = {"pause", "rewind", "fast_forward", "exit",
                                                      "states", "screen", "bfi", "controls",
                                                      "cheats", "apps", "volume"};
    // The others reach the sections' parts directly.
    static const std::vector<std::string> tiles = {"pause", "rewind", "fast_forward", "exit", "states",
                                                   "screen", "swap", "fps", "bfi", "options",
                                                   "buttons", "cheats", "apps", "volume"};
    // The bar holds the first five.
    static const std::vector<std::string> dock = {"pause", "rewind", "fast_forward", "states", "exit",
                                                  "screen", "swap", "fps", "bfi", "options",
                                                  "buttons", "cheats", "apps", "volume"};
    if (layout == "dock") {
        return dock;
    }
    if (layout == "grid" || layout == "compact" || layout == "split" || layout == "performance") {
        return tiles;
    }
    return standard;
}

std::string QuickSpec::Key() const {
    return plugin_id.empty() ? std::string() : plugin_id + "/" + choice_id;
}

Result<std::vector<QuickSpec>> ParseQuickSettings(std::string_view manifest_json) {
    const std::optional<JsonValue> root = ParseJson(manifest_json);
    const JsonValue* quick = root ? root->Find("quick_settings") : nullptr;
    if (quick == nullptr || quick->kind != JsonValue::Kind::Object) {
        return Status::Fail("the plugin describes no quick settings");
    }
    const std::string plugin_id = root->String("id");
    std::vector<QuickSpec> out;
    const auto add = [&](const JsonValue& value) {
        std::optional<QuickSpec> spec = ReadChoice(value, plugin_id);
        const bool taken = spec && std::any_of(out.begin(), out.end(), [&](const QuickSpec& s) {
                               return s.choice_id == spec->choice_id;
                           });
        if (spec && !taken && out.size() < MAX_CHOICES) {
            out.push_back(std::move(*spec));
        }
    };
    if (const JsonValue* layouts = quick->Find("layouts"); layouts != nullptr) {
        if (layouts->kind == JsonValue::Kind::Array) {
            for (const JsonValue& value : layouts->items) {
                add(value);
            }
        }
    } else {
        add(*quick);
    }
    if (out.empty()) {
        return Status::Fail("This version of EmuDEN has none of the plugin's quick settings layouts.");
    }
    return out;
}

std::vector<std::string> ArrangeQuickActions(const QuickSpec& spec, const std::vector<std::string>& available) {
    std::vector<std::string> wanted = spec.order;
    for (const std::string& id : DefaultQuickActions(spec.layout)) {
        if (!Has(wanted, id)) {
            wanted.push_back(id);
        }
    }
    std::vector<std::string> out;
    for (const std::string& id : wanted) {
        if (!Has(spec.hidden, id) && Has(available, id)) {
            out.push_back(id);
        }
    }
    return out;
}

} // namespace den::app
