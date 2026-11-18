// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Builders for the solids game objects are made of: moulded slabs with rounded, bevelled
// edges (smooth normals, so the edges catch the light), rings for discs and plain boxes for
// small details. Meshes are y-up, front towards +z.

#pragma once

#include <vector>

#include "den/gfx/mesh3d.h"

namespace den::gfx {

struct Vec2 {
    float x = 0, y = 0;
};

/// A rectangle `w` x `h` centred on the origin, counter-clockwise, with its top corners
/// rounded by `r_top` and its bottom ones by `r_bottom`. `notch` cuts the top-right corner
/// off at 45 degrees instead (both legs that long).
std::vector<Vec2> RoundedOutline(float w, float h, float r_top, float r_bottom, float notch = 0,
                                 int corner_segments = 6);

/// A rectangle `w` x `h` whose top corners step down by `drop` for `shoulder` from each side:
/// the outline of a handheld's cartridge. `r_top` rounds the top corners (of both steps),
/// `r_bottom` the bottom ones, `chamfer` cuts the steps' inner corners.
std::vector<Vec2> ShoulderedOutline(float w, float h, float shoulder, float drop, float r_top, float r_bottom,
                                    float chamfer, int corner_segments = 5);

struct SlabParts {
    Part front = Part::Shell;
    Part back = Part::Shell;
    Part side = Part::Shell;  // the walls
    Part spine = Part::Shell; // the wall facing -x (a case's spine)
    Part bevel = Part::Shell; // the rounded edges between the faces and the walls
};

/// A solid of the convex `outline`, `depth` thick around z = `cz`, its edges rounded by
/// `bevel`. The front face's uv spans the outline's bounding box; the spine's runs from back
/// to front (u) and top to bottom (v).
void AddSlab(std::vector<MeshVertex>& out, const std::vector<Vec2>& outline, float depth, float bevel,
             const SlabParts& parts, float cz = 0, float cx = 0, float cy = 0, int bevel_steps = 4);

/// A flat ring from `r_in` to `r_out`, `depth` thick around z = `cz`: `top` faces +z, `back`
/// faces -z, the rims are Shell. uv spans the outer circle.
void AddRing(std::vector<MeshVertex>& out, float r_in, float r_out, float depth, Part top, Part back,
             float cz = 0, int segments = 72);

/// Moulded relief on a flat face at height `z`: a band `width` wide inside the outline whose
/// normals slope like the walls of a pocket sunk into the face (or of a ridge standing out
/// of it when `raised`), so the light picks it out without cutting the face.
void AddGroove(std::vector<MeshVertex>& out, const std::vector<Vec2>& outline, float width, float z, float cx = 0,
               float cy = 0, Part part = Part::Shell, bool raised = false, float photo_width = 0);

/// A box centred on (cx, cy, cz) with sharp edges; every face is `part`.
void AddBox(std::vector<MeshVertex>& out, float w, float h, float d, Part part, float cx = 0, float cy = 0,
            float cz = 0);

} // namespace den::gfx
