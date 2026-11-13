// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A console with two screens (den/player/screens.h) in the player screen: one screen per
// display when the device has two, the layouts of one display otherwise, and the touches
// on whichever display shows the touch screen.
//
// Settings: "player.dual_screen.mode" (auto, top-on-main, top-on-second, single; layered,
// the device profile sets its default), "player.dual_screen.layout" (stacked, side-by-side,
// large-small, one; unset follows the window's shape) and "player.dual_screen.swap".

#pragma once

#include <array>
#include <string>
#include <vector>

#include "den/gfx/canvas.h"
#include "den/input/input.h"
#include "den/player/screens.h"
#include "den/ui/node.h"
#include "den/ui/widgets.h"

namespace den::app {

class DualScreens {
public:
    ~DualScreens();

    /// Every frame while the game runs: reads the game's screens and takes or gives back
    /// the second display.
    void Update();
    /// Gives the second display back to the companion pages.
    void Stop();
    /// The running game has two screens: DrawMain and Pointer are to be used.
    bool Active() const {
        return screens_.size() == 2;
    }
    /// One screen is on each display right now.
    bool Split() const {
        return split_;
    }
    /// The main display's part. `button` shows the swap button of the one-screen layout.
    void DrawMain(ui::Context& ctx, const gfx::Rect& room, bool integer, bool button);
    /// A press on the main display, in the canvas's units. True when it was for the swap
    /// button and not for the game.
    bool Pointer(const input::Event& event, float x, float y);
    void Swap();

    /// The rows of the pause menu's "Screens" page.
    std::vector<ui::RowSpec> Rows();
    std::string Summary() const;
    std::string Json() const;

private:
    void DrawSecond(ui::Context& ctx, const gfx::Rect& area);
    void Touch(int display, input::Event::Type type, float x, float y);
    void Note();

    std::vector<player::GameScreen> screens_;
    bool claimed_ = false;
    bool split_ = false;
    bool integer_ = false;
    // Where each screen was last drawn, per display (0 main, 1 second), in that canvas's units.
    std::array<std::array<gfx::Rect, 2>, 2> rects_{};
    gfx::Rect button_;
    gfx::Rect room_;
    int held_display_ = -1; // the display a finger is down on
    int held_screen_ = -1;
    double pressed_at_ = 0;
    double release_at_ = 0; // when a tap shorter than a frame is let go; 0 = none
    std::string noted_;
};

/// The Player section's rows for consoles with two screens.
std::vector<ui::RowSpec> DualScreenSettingRows();
/// For the test endpoint: {"screens":2,"split":true,...} of the game on screen, or "null".
std::string DualScreenJson();

} // namespace den::app
