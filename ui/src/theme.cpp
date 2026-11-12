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

const Theme& ThemeByName(const std::string& name) {
    return EqualsNoCase(name, "Dark") ? DarkTheme() : LightTheme();
}

std::vector<std::string> ThemeNames() {
    return {"Light", "Dark"};
}

} // namespace den::ui
