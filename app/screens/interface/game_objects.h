// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The objects games are shown as in the "shelf3d" layout: moulded cartridges and cards with a
// recessed label, keep cases with the cover under a clear sleeve, jewel cases with an inlay
// under a clear lid, and discs. Built as meshes one unit high, cached per proportions.

#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "den/gfx/mesh3d.h"
#include "interface_spec.h"

namespace den::app {

/// An ObjectLook with every default filled in.
struct GameObject {
    std::string shape;
    std::string style;      // a cartridge's mould: "plain", "pocket" or "pocket_wide"
    std::string label_edge; // "none", "paper" or "foil": the sticker's border round the cover
    bool smoke = false;     // see-through shell (its colour's alpha), the board showing inside
    bool photo = false;     // a cartridge or card whose front is a photograph of the real one
    float aspect = 0.7f, depth = 0.07f;
    gfx::Color shell;
    gfx::Rect label; // the sticker on the front, 0..1
    float radius = 0, notch = 0;
    int grip = 0;
    bool disc = false;

    /// The opaque parts, one unit high, centred on the origin, front towards +z.
    const gfx::Mesh3D& Mesh() const;
    /// The clear plastic (a jewel case's lid), drawn after the opaque parts; null for none.
    const gfx::Mesh3D* ClearMesh() const;
    /// The disc that slides out of a case, one unit across; null when the object has none.
    const gfx::Mesh3D* DiscMesh() const;
    /// The same cartridge or card with a photograph of the real one as its front, in the
    /// photograph's proportions and the plastic's colour.
    GameObject WithPhoto(gfx::Color plastic, float photo_aspect) const;
    /// A case or jewel case in the proportions of its own cover.
    GameObject WithCover(float cover_aspect) const;
    /// The part of the front the cover is printed on: the sticker inside its border.
    gfx::Rect CoverRect() const;
    /// The cover's width over its height, for cropping the cover onto it.
    float LabelAspect() const {
        const gfx::Rect c = CoverRect();
        return c.w * aspect / std::max(0.01f, c.h);
    }
};

GameObject Resolve(const ObjectLook& look);

} // namespace den::app
