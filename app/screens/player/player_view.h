// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pieces of the in-game layer that do not need the session: where the picture goes for a
// screen size, and the row of one state slot.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "den/gfx/canvas.h"
#include "den/ui/node.h"

namespace den::app {

enum class ScreenSize { Fit, PixelPerfect, FourThree, SixteenNine, Stretch };
const std::vector<std::string>& ScreenSizeNames();

struct Placement {
    gfx::Rect area;   // what the session draws into
    float aspect = 0; // the aspect override for the session; 0 = the core's own
};

/// `window` in logical units, `scale` pixels per unit, the frame in the core's pixels.
Placement Place(ScreenSize size, const gfx::Rect& window, int frame_width, int frame_height,
                float core_aspect, float scale);

/// One save-state slot: thumbnail, number and age.
class SlotRow final : public ui::Node {
public:
    /// `thumbnail` is the picture taken when the slot was saved; empty when there is none.
    SlotRow(int slot, bool filled, const std::filesystem::path& thumbnail);

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    std::string title_, detail_, thumbnail_;
    bool filled_ = false;
};

} // namespace den::app
