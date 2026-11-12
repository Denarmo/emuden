// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/base/files.h"
#include "den/ui/theme.h"

namespace den::ui {

namespace {

Theme Make(const char* name, gfx::Color page, gfx::Color fg, gfx::Color dialog) {
    Theme theme;
    theme.name = name;
    theme.page = page;
    theme.fg = fg;
    theme.text_dim = fg.WithAlpha(0.62f);
    theme.panel = fg.WithAlpha(0.07f);
    theme.panel_strong = fg.WithAlpha(0.16f);
    theme.scrim = gfx::Color{0, 0, 20, 120};
    theme.dialog = dialog;
    return theme;
}

} // namespace

const Theme& LightTheme() {
    static const Theme theme =
        Make("Light", gfx::Color::Rgb(0xffffff), gfx::Color::Rgb(0x0000ff), gfx::Color::Rgb(0xf3f3ff));
    return theme;
}

const Theme& DarkTheme() {
    static const Theme theme =
        Make("Dark", gfx::Color::Rgb(0x07071a), gfx::Color::Rgb(0xc9ceff), gfx::Color::Rgb(0x15152e));
    return theme;
}

namespace {

// The other named themes: the same object, another page and foreground.
const std::vector<Theme>& MoreThemes() {
    static const std::vector<Theme> themes = [] {
        std::vector<Theme> out{
            Make("Green", gfx::Color::Rgb(0xf6fff8), gfx::Color::Rgb(0x006b3c), gfx::Color::Rgb(0xe6f6ec)),
            Make("Magenta", gfx::Color::Rgb(0xfff7fd), gfx::Color::Rgb(0xb4008f), gfx::Color::Rgb(0xfbe8f6)),
            Make("Amber", gfx::Color::Rgb(0x161000), gfx::Color::Rgb(0xffc24a), gfx::Color::Rgb(0x2a2008)),
            Make("High contrast", gfx::Color::Rgb(0xffffff), gfx::Color::Rgb(0x000000), gfx::Color::Rgb(0xffffff)),
            Make("High contrast dark", gfx::Color::Rgb(0x000000), gfx::Color::Rgb(0xffffff),
                 gfx::Color::Rgb(0x000000)),
        };
        for (Theme& theme : out) {
            if (theme.name.starts_with("High contrast")) {
                theme.text_dim = theme.fg;
                theme.panel = theme.fg.WithAlpha(0.14f);
                theme.panel_strong = theme.fg.WithAlpha(0.34f);
            }
        }
        return out;
    }();
    return themes;
}

} // namespace

const Theme& ThemeByName(const std::string& name) {
    for (const Theme& theme : MoreThemes()) {
        if (EqualsNoCase(name, theme.name)) {
            return theme;
        }
    }
    return EqualsNoCase(name, "Dark") ? DarkTheme() : LightTheme();
}

std::vector<std::string> ThemeNames() {
    std::vector<std::string> names{"Light", "Dark"};
    for (const Theme& theme : MoreThemes()) {
        names.push_back(theme.name);
    }
    return names;
}

} // namespace den::ui
