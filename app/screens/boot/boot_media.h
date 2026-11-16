// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The game's cartridge, card or disc as the boot screen drops it into its slot.

#pragma once

#include "den/app/game_faces.h"
#include "den/app/game_objects.h"
#include "den/library/library.h"
#include "den/ui/node.h"

namespace den::app {

/// What the game is held on: Shelf's object for its console when Shelf is the Home
/// interface, else its console's usual media. A case with a disc gives the disc.
GameObject MediaFor(const library::Entry& game);

/// Where and how the media is drawn: the middle of its bottom edge at (x, bottom), `height`
/// high, turned by `yaw`, a disc spun by `spin`; nothing of it below `cut` shows.
struct MediaPose {
    float x = 0, bottom = 0, height = 0;
    float yaw = 0, spin = 0;
    float cut = 1e9f;
    float alpha = 1;
};

/// The width of the media over its height.
float MediaAspect(const GameObject& media);

void DrawMedia(ui::Context& ctx, const library::Entry& game, const GameObject& media, const LoadedFaces* faces,
               const MediaPose& pose);

} // namespace den::app
