// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A console's photograph from the front (Library::Device) made ready to be the face of a 3D
// console: cropped to the console, its clear ground filled with the plastic's colour, its
// outline taken, and copies with the plastic in each of the console's other colours.

#pragma once

#include <string>
#include <vector>

#include "den/gfx/mesh_shapes.h"
#include "den/gfx/types.h"

namespace den::app {

struct ConsolePhoto {
    std::vector<std::string> images; // the photograph, then one per colour asked for
    std::vector<gfx::Color> colors;  // the plastic's colour in each
    float aspect = 0;                // width over height; 0: none
    std::vector<gfx::Vec2> outline;  // convex, one high, centred on the origin, y up
};

/// Cleaned copies are kept under the cache and made only once. Blocking; empty when the
/// picture cannot be read or shows no console on a clear ground.
ConsolePhoto PrepareConsolePhoto(const std::string& path, const std::vector<gfx::Color>& colors);

} // namespace den::app
