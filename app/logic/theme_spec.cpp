// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "theme_spec.h"

#include <algorithm>
#include <optional>

#include "den/base/json.h"
#include "interface_spec.h"

namespace den::app {

namespace {

constexpr std::size_t MAX_THEMES = 24;

bool PlainId(std::string_view id) {
    return !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::optional<gfx::Color> Color(const JsonValue* colors, std::string_view key) {
    return colors != nullptr && colors->kind == JsonValue::Kind::Object ? ParseHexColor(colors->String(key))
                                                                        : std::nullopt;
}

std::optional<ui::Theme> ReadTheme(const JsonValue& value, const std::string& plugin_id) {
    if (value.kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    const std::string id = value.String("id");
    const JsonValue* colors = value.Find("colors");
    const auto page = Color(colors, "page");
    const auto fg = Color(colors, "fg");
    if (!PlainId(id) || !page || !fg) {
        return std::nullopt;
    }
    ui::Theme theme = ui::LightTheme(); // the sizes
    theme.key = plugin_id + "/" + id;
    theme.name = value.String("name").substr(0, 40);
    if (theme.name.empty()) {
        return std::nullopt;
    }
    theme.page = gfx::Color{page->r, page->g, page->b, 255}; // the page is always opaque
    theme.fg = *fg;
    theme.text_dim = Color(colors, "text_dim").value_or(fg->WithAlpha(0.62f));
    theme.panel = Color(colors, "panel").value_or(fg->WithAlpha(0.07f));
    theme.panel_strong = Color(colors, "panel_strong").value_or(fg->WithAlpha(0.16f));
    theme.scrim = Color(colors, "scrim").value_or(gfx::Color{0, 0, 20, 120});
    const gfx::Color dialog = Color(colors, "dialog").value_or(theme.page);
    theme.dialog = gfx::Color{dialog.r, dialog.g, dialog.b, 255};
    theme.accent = Color(colors, "accent").value_or(theme.accent);
    if (!ReadableTheme(theme)) {
        return std::nullopt;
    }
    return theme;
}

} // namespace

bool ReadableTheme(const ui::Theme& theme) {
    return ui::ContrastRatio(theme.fg, theme.page) >= MIN_TEXT_CONTRAST &&
           ui::ContrastRatio(theme.text_dim, theme.page) >= MIN_TEXT_CONTRAST;
}

Result<std::vector<ui::Theme>> ParseThemes(std::string_view manifest_json) {
    const std::optional<JsonValue> root = ParseJson(manifest_json);
    const JsonValue* themes = root ? root->Find("themes") : nullptr;
    if (themes == nullptr || themes->kind != JsonValue::Kind::Array) {
        return Status::Fail("the plugin has no themes");
    }
    const std::string plugin_id = root->String("id");
    std::vector<ui::Theme> out;
    for (const JsonValue& value : themes->items) {
        std::optional<ui::Theme> theme = ReadTheme(value, plugin_id);
        const bool taken = theme && std::any_of(out.begin(), out.end(), [&](const ui::Theme& t) {
                               return t.key == theme->key;
                           });
        if (theme && !taken && out.size() < MAX_THEMES) {
            out.push_back(std::move(*theme));
        }
    }
    if (out.empty()) {
        return Status::Fail("none of the plugin's themes is readable");
    }
    return out;
}

} // namespace den::app
