// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the builders of game objects share (game_objects.cpp, game_moulds.cpp).

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "den/gfx/mesh_shapes.h"
#include "game_objects.h"

namespace den::app {

struct Built {
    std::vector<gfx::MeshVertex> solid, clear;
    // The shell's surfaces: see-through ones go to the transparent pass.
    std::vector<gfx::MeshVertex>& Shell(const GameObject& o) {
        return o.smoke ? clear : solid;
    }
};

gfx::SlabParts All(gfx::Part part);
gfx::Part ShellPart(const GameObject& o);
/// The middle of a 0..1 rectangle of the front, in mesh units.
gfx::Vec2 Centre(const GameObject& o, const gfx::Rect& r);
/// The label on the face at `z`, the cover inside its paper or foil border.
void AddSticker(std::vector<gfx::MeshVertex>& out, const GameObject& o, float z);
/// The board inside a see-through shell.
void AddBoard(std::vector<gfx::MeshVertex>& out, const GameObject& o, bool wide);
/// Contacts along the back's bottom edge, and a screw.
void AddBack(std::vector<gfx::MeshVertex>& out, const GameObject& o, bool screw);
/// A ROM hack's "HACK" sticker over the label's top right corner, on the face at `z`.
void AddHackSticker(std::vector<gfx::MeshVertex>& out, const GameObject& o, float z);
/// Marks the vertices from `from` on as Part::Paint in `color`.
void PaintFrom(std::vector<gfx::MeshVertex>& out, std::size_t from, gfx::Color color);

/// A console's own mould (game_moulds.cpp): gives `o` the shape, proportions and label of
/// the mould named `style` and returns its plastic; nothing when there is no such mould.
std::optional<gfx::Color> ApplyMould(GameObject& o, const std::string& style);
/// Every mould's name, for reading looks.
const std::vector<std::string>& MouldNames();
/// The outline of the mould `o` is in; nothing when it is in none.
std::optional<std::vector<gfx::Vec2>> MouldOutline(const GameObject& o);
/// Builds `o` in its mould; false when it is in none.
bool BuildMould(Built& b, const GameObject& o);
/// A UMD: the shell (`clear`: its window) round a disc one unit across.
void BuildUmdShell(std::vector<gfx::MeshVertex>& solid, std::vector<gfx::MeshVertex>& clear);

} // namespace den::app
