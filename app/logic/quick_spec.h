// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The layouts of the quick settings (the second screen while a game runs) and what an
// interface plugin may say about them (plugin.json, "quick_settings"; docs/quick-settings.md
// in emuden-plugins). Data only: the layouts are the app's code, a plugin picks one, orders
// or hides its actions and sets its colours.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"
#include "den/gfx/types.h"

namespace den::app {

/// Every action id the quick settings know, in EmuDEN's order. Not all are there at every
/// moment (rewind needs a core that rewinds, cheats a game that takes them).
const std::vector<std::string>& QuickActionIds();
bool KnownQuickAction(std::string_view id);

/// The layouts: "standard" (EmuDEN's own: a card with the cover and the controls, then rows
/// one step in), "grid" (big tiles), "compact" (one column of icon rows), "dock" (a large
/// cover with a bar of the main actions and a drawer for the rest) and "split" (controls at
/// the left, the state slots at the right).
bool KnownQuickLayout(std::string_view layout);
/// The actions a layout shows, in its order, when a plugin says nothing.
const std::vector<std::string>& DefaultQuickActions(std::string_view layout);

struct QuickSpec {
    std::string plugin_id; // empty: EmuDEN's own standard layout
    std::string choice_id; // the choice within the plugin
    std::string name;      // as Settings lists it
    std::string layout = "standard";

    std::optional<gfx::Color> background, text, dim, panel, accent;

    std::vector<std::string> order;  // these first, the layout's others after them
    std::vector<std::string> hidden; // never shown
    float tile_size = 1.0f;          // tiles and rows, 0.75 to 1.5
    int columns = 0;                 // grid: 2 to 6; 0 picks by the screen's shape
    int dock = 5;                    // dock: actions in the bar, 3 to 7
    bool cover = true;               // the game's cover shows

    /// The setting value that picks it: "<plugin>/<choice>", or "" for the standard layout.
    std::string Key() const;
};

/// The choices of an interface plugin's "quick_settings": one per entry of its "layouts",
/// or the object itself as one. Choices whose layout this app does not have are left out;
/// it fails when none is left or there is no such object.
Result<std::vector<QuickSpec>> ParseQuickSettings(std::string_view manifest_json);

/// The ids `spec` shows, in order, of those `available` now.
std::vector<std::string> ArrangeQuickActions(const QuickSpec& spec, const std::vector<std::string>& available);

} // namespace den::app
