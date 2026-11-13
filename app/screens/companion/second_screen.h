// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen of the app (pal::SecondDisplay): normally the companion pages, drawn by
// a stage of its own (the focused game while browsing, quick settings while playing). The
// player can take the screen over for a console that has two screens of its own.
//
// Setting "ui.companion.enabled": "auto" (on when the device has a second screen), "on",
// "off".

#pragma once

#include <functional>
#include <string>

#include "den/gfx/types.h"
#include "den/input/input.h"
#include "den/ui/node.h"
#include "den/ui/page.h"

namespace den::app {

/// Who draws on the second screen instead of the companion pages.
struct SecondScreenClient {
    /// Every frame the screen can be drawn on, before the main window's frame. The canvas
    /// is begun for the second screen (`area` is all of it, in the canvas's units); the GL
    /// context is the app's, so a texture of the main screen can be drawn here. Like
    /// player::Session::Draw it may Flush, use GL and RestoreState.
    std::function<void(ui::Context& ctx, const gfx::Rect& area)> draw;
    /// PointerDown, PointerMove and PointerUp on the second screen; x and y are pixels of
    /// that screen (divide by SecondScreenScale() for the units `draw` works in).
    std::function<void(const input::Event& event)> pointer;
};

/// A second screen has a surface right now. It can go at any time (display unplugged, app
/// in the background); a client then gets no draw calls until it is back.
bool SecondScreenReady();
/// The size in pixels, and pixels per unit of the canvas the client draws with.
void SecondScreenPixelSize(int& width, int& height);
float SecondScreenScale();
/// Takes the screen for `client` (also when the companion is switched off) until
/// ReleaseSecondScreen. False when the device has no second screen; nothing is taken then.
bool ClaimSecondScreen(SecondScreenClient client);
void ReleaseSecondScreen();
bool SecondScreenClaimed();

/// Draws on the second screen in place of whatever has it (the companion pages or a claim)
/// and gets its presses, until ClearSecondScreenOverlay: the pause menu while it is open
/// there, or the game when the displays are swapped. It does not open the screen by itself.
void SetSecondScreenOverlay(SecondScreenClient client);
void ClearSecondScreenOverlay();
/// The companion pages on another canvas (begun by the caller): the main window while the
/// game has the second screen. They are ticked and drawn, toasts and questions included.
void DrawCompanionStage(ui::Context& ctx);

// For the companion pages (this folder).
/// The stage the companion pages live on.
ui::Stage& CompanionStage();
/// The library entry the main screen is about: the focused game, or the one last focused.
const std::string& CompanionGame();
/// A page with no selection border (touch only) fills its root; shared by the pages.
constexpr float COMPANION_HEIGHT = 600.0f; // logical units from top to bottom

} // namespace den::app
