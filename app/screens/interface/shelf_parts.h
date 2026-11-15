// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the "shelf3d" layout draws a game with, shared by its row and the pages that show a
// game's object elsewhere (the second screen): the renderer, the object shaped by the game's
// own pictures, where a cartridge and its box stand, and the material they are drawn with.

#pragma once

#include <memory>
#include <optional>

#include "den/gfx/mesh3d.h"
#include "den/library/library.h"
#include "den/ui/node.h"
#include "game_faces.h"
#include "game_objects.h"

namespace den::app {

inline constexpr float CART_SHARE = 0.56f; // a cartridge before its box, in box heights

/// The 3D renderer, set up on first use; null when it cannot be (the layout is drawn flat).
gfx::Renderer3D* ShelfRenderer();

/// The front's width over its height of a game's objects together.
float Footprint(const GameObject& object, const GameObject* box);

/// The console's look shaped by the game's own pictures: a cartridge in its photograph's
/// proportions and plastic, a case in its cover's. Nothing when they change nothing.
std::optional<GameObject> OwnObject(const GameObject& look, const LoadedFaces& faces);

/// One object of a game relative to its anchor (the middle of its bottom): across, forward
/// and its height, for a game `height` high.
struct ShelfPiece {
    float dx = 0, dz = 0, height = 0;
};
ShelfPiece CartPieceOf(float height, const GameObject& object, const GameObject* box);
ShelfPiece BoxPieceOf(float height, const GameObject& object);

/// What the game's object (or, with `box`, its cardboard box) is drawn with. `beside_box`:
/// the cartridge stands before a box, which carries the back and spine.
gfx::Material ShelfMaterial(ui::Context& ctx, const library::Entry& game, const GameObject& object,
                            const LoadedFaces* faces, bool box, bool beside_box);

/// What a game's disc is drawn with (a hack's: the original game's), and a UMD's shell.
gfx::Material DiscMaterial(ui::Context& ctx, const library::Entry& game, const GameObject& object,
                           const LoadedFaces* faces);

/// Shelf's object of a game, standing still, slightly turned, in its rect: for a page that
/// shows the game when Shelf is the Home interface.
std::unique_ptr<ui::Node> MakeShelfPreview(const library::Entry& game, float width, float height);

} // namespace den::app
