// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Generic consoles for the launch animation of the "shelf3d" layout: a portrait and a
// landscape handheld, one that folds open, and one that stands under a television. None is a
// copy of a real console; they share the layout the real ones had (screen above, pad on the
// left, buttons on the right, the cartridge slot at the top of the back).

#pragma once

#include <string>

#include "console_photo.h"
#include "den/gfx/mesh3d.h"

namespace den::app {

struct ConsoleModel {
    const gfx::Mesh3D* mesh = nullptr; // one unit high, centred on the origin, front towards +z
    float aspect = 1, depth = 0.2f;    // its front's width over its height, its thickness
    // The slot: the cartridge goes in at this height and depth (both in console heights),
    // standing this tall (in console heights), this far into the console.
    float slot_y = 0.5f, slot_z = -0.05f, cart_height = 0.44f, sink = 0.55f;
    bool turns = true; // the slot is at the back: shown from behind, turned round to start
    float bottom = 0.5f; // how far below its origin it reaches, in console heights
};

/// The console of that name ("portrait", "landscape", "clamshell", "home"); null mesh for
/// another.
ConsoleModel ConsoleFor(const std::string& name);

/// The console of that name with the photograph's outline and front, its screen at `screen`
/// (0..1 of the photograph; none when empty); null mesh when there is no photograph.
ConsoleModel PhotoConsole(const ConsolePhoto& photo, const std::string& name, const gfx::Rect& screen);

} // namespace den::app
