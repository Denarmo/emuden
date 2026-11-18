// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cleaning the pictures of a game's faces before they go on a 3D object: scans come with white
// margins, photographs of cartridges and discs on a plain ground, spines lying on their side.

#pragma once

#include "den/gfx/images.h"
#include "den/gfx/types.h"

namespace den::app {

enum class FaceKind {
    Cover,   // a box's front: margins cropped
    Back,    // a box's back: margins cropped
    Spine,   // margins cropped, turned upright when it lies on its side
    Cart,    // a photographed cartridge: the ground made clear, cropped to the cartridge
    Disc,    // a disc: the ground made clear (including spindle hole), cropped and squared
    UmdDisc, // a PSP UMD: extracts the circular 60 mm disc from a caddy photo when present
};

/// Cleans `image` in place for a face of that kind. False when nothing usable is left.
bool CleanFace(gfx::ImageData& image, FaceKind kind);

/// What a cleaned photograph of a cartridge tells: the plastic's colour along its edge, its
/// width over its height, and the label, 0..1 of the picture (empty when it cannot be told:
/// no part of the picture stands out from the plastic as a large rectangle), plus whether
/// the picture appears to be a bare label scan rather than a full cartridge photo.
struct CartReading {
    gfx::Color plastic;
    float aspect = 0;
    gfx::Rect label{0, 0, 0, 0};
    bool label_only = false;
};
CartReading ReadCart(const gfx::ImageData& image);

} // namespace den::app
