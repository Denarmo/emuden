// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pieces the console models share.

#pragma once

#include <vector>

#include "console_models.h"
#include "den/gfx/mesh_shapes.h"

namespace den::app {

/// The part of the convex `outline` where a * x + b * y <= limit; empty when none is.
std::vector<gfx::Vec2> ClipOutline(const std::vector<gfx::Vec2>& outline, float a, float b, float limit);

/// A console's body `depth` thick with the pocket of `model` in the top of its back: the
/// front part whole, the back part round the pocket (below its lip, and the shoulders
/// beside it), so the cartridge stands in it with its back flush with the console's.
void PocketBody(std::vector<gfx::MeshVertex>& out, const std::vector<gfx::Vec2>& outline, float depth, float bevel,
                gfx::SlabParts front, const ConsoleModel& model);

} // namespace den::app
