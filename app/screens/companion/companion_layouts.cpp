// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The list layouts of the quick settings: EmuDEN's standard one (the card, then the rows one
// step in) and "compact" (one column of icon rows, the volumes inline).

#include <algorithm>
#include <string_view>

#include "companion_quick.h"
#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/pages.h"

namespace den::app {

namespace {

constexpr float BUTTON_HEIGHT = 56;
constexpr float CARD_COVER_W = 112, CARD_COVER_H = 150;
constexpr float ROW_HEIGHT = 64;

// The standard card's controls.
bool CardControl(std::string_view id) {
    return id == "pause" || id == "rewind" || id == "fast_forward" || id == "exit";
}

// How a list shows an action that has no rows of its own (one a plugin moved there).
std::vector<ui::RowSpec> RowsOf(const QuickAction& action) {
    if (!action.rows.empty()) {
        return action.rows;
    }
    if (!action.value.empty()) {
        return {ValueRow(action.node_id, action.label, [value = action.value] { return value; }, action.run)};
    }
    return {ActionRow(action.node_id, action.label, action.run)};
}

} // namespace

const QuickAction* QuickContext::Find(std::string_view id) const {
    const auto it = std::find_if(actions.begin(), actions.end(), [&](const QuickAction& a) { return a.id == id; });
    return it == actions.end() ? nullptr : &*it;
}

std::string QuickContext::Status() const {
    return snapshot.state == player::State::Paused ? "Paused"
           : snapshot.rewinding                    ? "Rewinding"
           : snapshot.fast_forward                 ? "Fast-forward"
                                                   : "Playing";
}

const QuickLayout& FindQuickLayout(std::string_view layout) {
    static const QuickLayout standard{BuildStandardLayout};
    static const QuickLayout compact{BuildCompactLayout};
    static const QuickLayout grid{BuildGridLayout};
    static const QuickLayout dock{BuildDockLayout, BuildDockDrawer};
    static const QuickLayout split{BuildSplitLayout};
    return layout == "compact" ? compact
           : layout == "grid"  ? grid
           : layout == "dock"  ? dock
           : layout == "split" ? split
                               : standard;
}

void BuildStandardLayout(QuickContext& ctx) {
    Services& svc = Svc();
    const library::Entry* game = ctx.game;
    // One list, so a display too low for every row scrolls.
    ui::Node& root = *ctx.root.Add(ui::MakeList(10));
    root.id = "companion.page";
    root.grow = 1;
    ctx.beside(root);

    // The card: the cover, what is playing, and its controls.
    ui::Node* card = root.Add(ui::MakeBox(ui::Axis::Row, 18));
    card->pad_bottom = 6;
    if (ctx.spec.cover) {
        auto* cover = card->Emplace<CoverView>(game ? game->name : std::string("Game"),
                                               game ? game->cover : std::string(), CARD_COVER_W, CARD_COVER_H);
        cover->overlay_title = game && game->placeholder_cover;
        cover->hack = game && IsHackGame(*game);
    }
    ui::Node* info = card->Add(ui::MakeBox(ui::Axis::Column, 6));
    info->grow = 1;
    info->justify = ui::Align::Center;
    info->Emplace<ui::Label>(game ? game->name : std::string("Game"), svc.theme->text_heading, gfx::FONT_BOLD);
    if (game) {
        std::string line = SystemName(game->system);
        if (game->play_seconds > 0) {
            line += " · " + TrF("{} played", PlayTimeText(game->play_seconds));
        }
        info->Emplace<ui::Label>(line)->tone = ui::Tone::Dim;
    }
    info->Emplace<ui::Label>(ctx.Status(), svc.theme->text_small, gfx::FONT_BOLD)->tone = ui::Tone::Dim;

    ui::Node* controls = info->Add(ui::MakeBox(ui::Axis::Row, 10));
    controls->pad_top = 6;
    std::vector<ui::RowSpec> rows;
    for (const QuickAction& action : ctx.actions) {
        if (!CardControl(action.id)) {
            // The sections, each one step in, with what it holds now at the right.
            for (ui::RowSpec& row : RowsOf(action)) {
                rows.push_back(std::move(row));
            }
            continue;
        }
        auto* button = controls->Emplace<ui::Button>(action.label, action.run);
        button->id = action.node_id;
        button->height = BUTTON_HEIGHT * ctx.spec.tile_size;
        button->grow = 1;
        button->selected = action.selected;
    }
    AddRows(&root, std::move(rows), ROW_HEIGHT * ctx.spec.tile_size);
}

void BuildCompactLayout(QuickContext& ctx) {
    const float scale = ctx.spec.tile_size;
    ui::Node& root = *ctx.root.Add(ui::MakeList(8));
    root.id = "companion.page";
    root.grow = 1;
    ctx.beside(root);
    AddQuickCover(root, ctx, 48, 64, true);
    // Two columns where the screen is wide enough, one otherwise; the volumes stay whole.
    const bool two = ctx.width > 0 && ctx.height > 0 && ctx.width / ctx.height >= 1.45f;
    ui::Node* pair = nullptr;
    for (const QuickAction& action : ctx.actions) {
        if (action.kind == QuickAction::Kind::Sliders) {
            pair = nullptr;
            AddRows(&root, action.rows, ROW_HEIGHT * scale);
            continue;
        }
        ui::Node* parent = &root;
        if (two) {
            if (pair == nullptr || pair->Children().size() == 2) {
                pair = root.Add(ui::MakeBox(ui::Axis::Row, 8));
            }
            parent = pair;
        }
        QuickTile* tile = AddQuickTile(*parent, action, true, scale);
        tile->height = 52 * scale;
        if (two) {
            tile->grow = 1;
            tile->width = 0;
        }
    }
    // An odd last row keeps its tile as wide as the others.
    if (pair != nullptr && pair->Children().size() == 1) {
        ui::Node* filler = pair->Add(ui::MakeBox(ui::Axis::Row));
        filler->grow = 1;
        filler->width = 0;
    }
}

} // namespace den::app
