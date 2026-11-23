// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The quick settings as actions with stable ids (quick_spec.h) and the layouts that arrange
// them on the main view of "companion.play". The sections the actions open (states, screen
// and picture, controls, volume) are the page's own and the same for every layout.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "den/library/library.h"
#include "den/player/session.h"
#include "den/ui/node.h"
#include "den/ui/widgets.h"
#include "quick_spec.h"

namespace den::app {

/// The views of the page: the main one, and the sections it opens.
enum class QuickView { Main, States, Screen, Picture, Controls, Volume, More };

struct QuickAction {
    enum class Kind {
        Control, // acts at once (pause, swap)
        Section, // opens a view or a page one step in
        Toggle,  // on or off (black frame insertion)
        Sliders, // rows of sliders (the volumes): inline in a list, a section in a grid
    };
    std::string id;      // "pause"
    std::string node_id; // "companion.menu": the id the standard layout gives its node
    std::string label;   // "Pause", "Resume"
    std::string value;   // what it holds now ("3 saved"), or empty
    Kind kind = Kind::Control;
    bool selected = false; // on: rewinding, fast-forward, a toggle that is on
    bool enabled = true;   // false: `refusal` says why
    std::string refusal;
    std::function<void()> run;
    /// How a list shows it (standard and compact layouts); empty for the card's controls.
    std::vector<ui::RowSpec> rows;
};

/// What a layout builds the main view with.
struct QuickContext {
    QuickContext(ui::Node& root_, const player::Snapshot& snapshot_, const library::Entry* game_, const QuickSpec& spec_)
        : root(root_), snapshot(snapshot_), game(game_), spec(spec_) {}

    ui::Node& root;
    const player::Snapshot& snapshot;
    const library::Entry* game;
    const QuickSpec& spec;
    std::vector<QuickAction> actions; // arranged: the spec's order, those there now
    float width = 0, height = 0;      // the page's area in units (0 before it was drawn)
    /// The way back to an app the settings lie over, as the first row of `parent`.
    std::function<void(ui::Node& parent)> beside;
    std::function<void(QuickView)> open;
    /// The state slots with their load/save switch, filling `parent`, `columns` a row (split).
    std::function<void(ui::Node& parent, int columns)> states;

    const QuickAction* Find(std::string_view id) const;
    /// The status line of the game: "Paused", "Rewinding", "Fast-forward", "Playing".
    std::string Status() const;
};

/// A layout: the main view, and the drawer it opens (QuickView::More) when it has one.
struct QuickLayout {
    void (*main)(QuickContext& ctx) = nullptr;
    void (*more)(QuickContext& ctx) = nullptr;
};
/// The standard layout for an unknown name.
const QuickLayout& FindQuickLayout(std::string_view layout);

void BuildStandardLayout(QuickContext& ctx); // companion_layouts.cpp
void BuildCompactLayout(QuickContext& ctx);
void BuildGridLayout(QuickContext& ctx); // companion_layouts_tiles.cpp
void BuildDockLayout(QuickContext& ctx);
void BuildDockDrawer(QuickContext& ctx);
void BuildSplitLayout(QuickContext& ctx);
void BuildPerformanceLayout(QuickContext& ctx); // companion_layouts_perf.cpp

// The parts the layouts share (companion_quick_parts.cpp).

/// A drawn icon for an action id: no image files, the theme's ink.
void DrawQuickIcon(ui::Context& ctx, std::string_view id, const gfx::Rect& box, gfx::Color ink);

/// A tap target for an action: its icon, label and value. `wide` puts them in a line
/// (compact rows, the dock), otherwise the icon sits above the label (tiles).
class QuickTile final : public ui::Node {
public:
    QuickTile(const QuickAction& action, bool wide, float scale);

protected:
    ui::Size MeasureContent(ui::Context& ctx) override;
    void DrawContent(ui::Context& ctx) override;

private:
    std::string action_id_, label_, value_;
    bool wide_ = false, selected_ = false, enabled_ = true;
    float scale_ = 1.0f;
};

/// A tile for `action`, its node id, its action (a disabled one says why in a toast).
QuickTile* AddQuickTile(ui::Node& parent, const QuickAction& action, bool wide, float scale);

/// The cover, the game's name and its status, in a column `height` high (0: as measured).
void AddQuickCover(ui::Node& parent, const QuickContext& ctx, float cover_w, float cover_h, bool text);

} // namespace den::app
