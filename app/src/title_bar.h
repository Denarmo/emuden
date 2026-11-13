// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "den/input/input.h"
#include "den/platform/pal.h"
#include "den/ui/node.h"

namespace den::app {

/// The app's own title bar where the platform leaves it to the app (pal::Caps::custom_frame):
/// the mark and the name at the left, minimise, maximise and close at the right. It is drawn
/// over the top of the window while the window is not full screen; the screens sit below it.
class TitleBar {
public:
    static constexpr float HEIGHT = 34; // logical units

    /// Pixels it takes this frame (0 when the window has the system's frame or fills the
    /// screen); `scale` is pixels per logical unit.
    int PixelHeight(const pal::Window& window, float scale) const;

    /// Pointer events over the bar are its own: true when `event` was used here. `quit` is
    /// set by the close button.
    bool HandleEvent(const input::Event& event, pal::Window& window, bool& quit);

    /// Draws the bar into a canvas begun over the whole window, and tells the window where
    /// it may be dragged.
    void Draw(ui::Context& ctx, pal::Window& window, int width_px, int bar_px);

private:
    enum Button { NONE = -1, MINIMIZE, MAXIMIZE, CLOSE, COUNT };
    Button At(float x_px, float y_px) const;

    int width_px_ = 0, bar_px_ = 0;
    float scale_ = 1;
    Button hover_ = NONE, pressed_ = NONE;
    bool hit_test_set_ = false;
};

} // namespace den::app
