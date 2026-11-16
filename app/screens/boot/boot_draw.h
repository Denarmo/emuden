// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the boot screen shows at each moment: the drop into the slot, the plain background
// and EmuDEN's logo sliding down onto it.

#pragma once

#include <memory>
#include <optional>

#include "den/app/game_faces.h"
#include "den/app/game_objects.h"
#include "den/library/library.h"
#include "den/ui/node.h"

namespace den::app {

/// Seconds from the start.
struct BootTimes {
    float lifted = 0;   // the media is over the slot
    float dropped = 0;  // it is in
    float closed = 0;   // the slot has closed
    float plain = 0;    // the screens are the plain background
    float sound = 0;    // the logo's sound begins (its chord lands with the logo)
    float landed = 0;   // the logo is down
    float logo_end = 0; // the earliest moment the game is shown
    // The game starts: once the logo is down, as loading a core holds the frames still.
    float start = 0;
};

struct BootDraw {
    library::Entry game;
    bool animate = true;
    float t = 0;

    void Begin(const library::Entry& game, bool animate, std::optional<gfx::Rect> from, float window_h);
    const BootTimes& Times() const;
    /// `out`: 0..1 through the fade at the end (into the game, or back to the page).
    void Main(ui::Context& ctx, float out);
    void Second(ui::Context& ctx, float out) const;

private:
    float Cover() const; // how much the background covers the screens
    void DrawDrop(ui::Context& ctx);
    void DrawLogo(ui::Context& ctx, float alpha);

    GameObject media_;
    std::unique_ptr<FaceLoader> faces_;
    std::optional<gfx::Rect> from_;
    bool shaped_ = false;
};

} // namespace den::app
