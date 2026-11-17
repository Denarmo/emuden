// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen at rest: while a game plays on the main screen only and the quick
// settings here go untouched for a while, they fade to a dark screen with EmuDEN's logo, so
// they do not draw the eye. The first touch only wakes them.

#pragma once

#include <vector>

#include "den/platform/second_display.h"
#include "den/ui/node.h"
#include "den/ui/widgets.h"

namespace den::app {

/// "ui.companion.rest": the quick settings rest when untouched (on unless set).
inline constexpr const char* COMPANION_REST_KEY = "ui.companion.rest";

class CompanionRest {
public:
    /// Each frame. `can_rest`: the quick settings are shown and nothing else asks for the
    /// screen. The touches of the press that wakes it are taken out of `pointer`.
    void Update(double now, bool can_rest, std::vector<pal::SecondPointer>& pointer);
    /// Fully at rest: the page need not be drawn under it.
    bool Resting() const {
        return amount_ >= 1.0f;
    }
    /// Over the page, as far as it has faded.
    void Draw(ui::Context& ctx) const;

private:
    double last_touch_ = -1;
    double last_frame_ = 0;
    float amount_ = 0; // 0 awake, 1 at rest
    bool waking_ = false; // the press that woke it has not ended
};

/// The row of the second screen's settings.
ui::RowSpec CompanionRestRow();

} // namespace den::app
