// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "den/gfx/types.h"

namespace den::ui {

/// Every colour and size the kit draws with. Widgets read the theme at draw time, so switching
/// it restyles the whole app. The selection accent is not here on purpose: it exists only in
/// the selection border (selection.cpp).
struct Theme {
    std::string name;
    gfx::Color page;         // the page background
    gfx::Color fg;           // text, icons and the base of every tint
    gfx::Color text_dim;     // secondary text
    gfx::Color panel;        // soft translucent tint of fg: tiles, rows, rail
    gfx::Color panel_strong; // a stronger tint: pressed and filled parts, tracks
    gfx::Color scrim;        // behind dialogs
    gfx::Color dialog;       // opaque dialog and toast surface

    float radius = 14;      // panels and tiles
    float radius_small = 8; // badges, switches
    float gap = 12;
    float margin = 32; // page margins

    float text_title = 34;
    float text_heading = 22;
    float text_body = 18;
    float text_small = 14;
};

const Theme& LightTheme();
const Theme& DarkTheme();
/// The theme with that name (case-insensitive); the default (Light) when unknown.
const Theme& ThemeByName(const std::string& name);
std::vector<std::string> ThemeNames();

} // namespace den::ui
