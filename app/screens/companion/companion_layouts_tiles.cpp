// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The touch layouts of the quick settings: "grid" (big tiles, the cover small in a corner),
// "dock" (the game large, a bar of the main actions and a drawer for the rest) and "split"
// (the controls on one side, the state slots on the other). Each fits the area it was built
// for: the page builds it again when the shape of that area changes.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

#include "companion_cells.h"
#include "companion_quick.h"
#include "den/app/app.h"
#include "den/app/game_widgets.h"
#include "den/app/pages.h"
#include "den/base/files.h"

namespace den::app {

namespace {

constexpr float GAP = 12;
constexpr float ROW_HEIGHT = 64;
constexpr float PADDING = 32; // the page's, top and bottom

float Aspect(const QuickContext& ctx) {
    return ctx.width > 0 && ctx.height > 0 ? ctx.width / ctx.height : 1.6f;
}

float Height(const QuickContext& ctx) {
    return ctx.height > 0 ? ctx.height : 600.0f;
}

// Rows of `columns` tiles in a scrolling list; the rows share `room` when that is enough.
void TileRows(ui::Node& list, const std::vector<const QuickAction*>& actions, int columns, float room, float scale) {
    const int count = static_cast<int>(actions.size());
    const int rows = (count + columns - 1) / columns;
    const float least = 96 * scale;
    const float height = rows > 0 ? std::max(least, (room - GAP * static_cast<float>(rows - 1)) / static_cast<float>(rows))
                                  : least;
    for (int first = 0; first < count; first += columns) {
        ui::Node* row = list.Add(ui::MakeBox(ui::Axis::Row, GAP));
        row->height = std::min(height, 180 * scale);
        for (int i = 0; i < columns; ++i) {
            ui::Node* cell = first + i < count ? AddQuickTile(*row, *actions[static_cast<std::size_t>(first + i)], false, scale)
                                               : row->Add(ui::MakeBox(ui::Axis::Row));
            cell->grow = 1;
            cell->width = 0;
        }
    }
}

std::vector<const QuickAction*> Pointers(const std::vector<QuickAction>& actions, std::size_t from = 0,
                                         std::size_t to = ~std::size_t{0}) {
    std::vector<const QuickAction*> out;
    for (std::size_t i = from; i < std::min(to, actions.size()); ++i) {
        out.push_back(&actions[i]);
    }
    return out;
}

// A list of wide tiles, the volumes as their sliders.
void WideList(ui::Node& list, const std::vector<const QuickAction*>& actions, float scale) {
    for (const QuickAction* action : actions) {
        if (action->kind == QuickAction::Kind::Sliders) {
            AddRows(&list, action->rows, ROW_HEIGHT * scale);
        } else {
            AddQuickTile(list, *action, true, scale)->height = 56 * scale;
        }
    }
}

// The newest state's picture, or empty.
std::string NewestState() {
    std::string newest;
    std::filesystem::file_time_type when{};
    for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
        if (!Svc().player->HasState(slot)) {
            continue;
        }
        const std::filesystem::path picture = Svc().player->StateThumbnail(slot);
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(picture, ec);
        if (!ec && (newest.empty() || time > when)) {
            newest = PathToUtf8(picture);
            when = time;
        }
    }
    return newest;
}

} // namespace

void BuildGridLayout(QuickContext& ctx) {
    const float scale = ctx.spec.tile_size;
    ui::Node& root = *ctx.root.Add(ui::MakeList(GAP));
    root.id = "companion.page";
    root.grow = 1;
    ctx.beside(root);
    // The cover small in the corner, with what is playing.
    AddQuickCover(root, ctx, 54, 72, true);
    const float aspect = Aspect(ctx);
    const int columns = ctx.spec.columns > 0 ? ctx.spec.columns : aspect < 0.95f ? 3 : aspect < 1.45f ? 4 : 5;
    const float room = Height(ctx) - PADDING - 92 - GAP; // under the cover line
    TileRows(root, Pointers(ctx.actions), columns, room, scale);
}

void BuildDockLayout(QuickContext& ctx) {
    const float scale = ctx.spec.tile_size;
    ui::Node& root = *ctx.root.Add(ui::MakeBox(ui::Axis::Column, GAP));
    root.id = "companion.page";
    root.grow = 1;
    ctx.beside(root);

    const float bar = 92 * scale;
    // The game, as large as the room above the bar allows, and the newest state beside it.
    ui::Node* stage = root.Add(ui::MakeBox(ui::Axis::Row, 18));
    stage->grow = 1;
    stage->align = ui::Align::Center;
    const float tall = std::clamp(Height(ctx) - PADDING - bar - GAP * 2, 120.0f, 420.0f) * 0.92f;
    if (ctx.spec.cover) {
        const library::Entry* game = ctx.game;
        auto* cover = stage->Emplace<CoverView>(game ? game->name : std::string("Game"),
                                                game ? game->cover : std::string(), tall * 0.75f, tall);
        // The title is drawn only on the plain panel shown when there is no picture at all.
        cover->overlay_title = false;
        cover->hack = game && IsHackGame(*game);
    }
    ui::Node* side = stage->Add(ui::MakeBox(ui::Axis::Column, 10));
    side->grow = 1;
    side->justify = ui::Align::Center;
    side->align = ui::Align::Start;
    QuickSpec plain = ctx.spec;
    plain.cover = false;
    QuickContext about{*side, ctx.snapshot, ctx.game, plain};
    AddQuickCover(*side, about, 0, 0, true);
    if (const std::string state = NewestState(); !state.empty() && ctx.Find("states") && Aspect(ctx) >= 1.1f) {
        auto* slot = side->Emplace<CompanionSlot>("Latest state", state);
        slot->id = "companion.latest_state";
        slot->height = std::min(240.0f, tall * 0.55f);
        slot->width = slot->height * 1.4f;
        slot->on_activate = ctx.Find("states")->run;
    }

    // The bar: the first actions, then the drawer with the others.
    const std::size_t count = std::min(ctx.actions.size(), static_cast<std::size_t>(ctx.spec.dock));
    ui::Node* dock = root.Add(ui::MakeBox(ui::Axis::Row, GAP));
    dock->height = bar;
    for (const QuickAction* action : Pointers(ctx.actions, 0, count)) {
        QuickTile* tile = AddQuickTile(*dock, *action, false, scale * 0.9f);
        tile->grow = 1;
        tile->width = 0;
    }
    if (ctx.actions.size() > count) {
        QuickAction more;
        more.id = "more";
        more.node_id = "companion.more";
        more.label = "More";
        more.value = std::format("{} more", ctx.actions.size() - count);
        more.run = [open = ctx.open] { open(QuickView::More); };
        QuickTile* tile = AddQuickTile(*dock, more, false, scale * 0.9f);
        tile->grow = 1;
        tile->width = 0;
    }
}

void BuildDockDrawer(QuickContext& ctx) {
    const float scale = ctx.spec.tile_size;
    const std::size_t count = std::min(ctx.actions.size(), static_cast<std::size_t>(ctx.spec.dock));
    ui::Node& list = *ctx.root.Add(ui::MakeList(8));
    list.id = "companion.drawer";
    list.grow = 1;
    const std::vector<const QuickAction*> rest = Pointers(ctx.actions, count);
    if (Aspect(ctx) >= 1.1f) {
        // Tiles where there is room, the volumes after them as sliders.
        std::vector<const QuickAction*> tiles, sliders;
        for (const QuickAction* action : rest) {
            (action->kind == QuickAction::Kind::Sliders ? sliders : tiles).push_back(action);
        }
        const int columns = Aspect(ctx) < 1.45f ? 3 : 4;
        const float headed = Height(ctx) - PADDING - 70; // under the section's heading
        TileRows(list, tiles, columns, headed - (ROW_HEIGHT + GAP) * static_cast<float>(sliders.size()) - GAP, scale);
        WideList(list, sliders, scale);
    } else {
        WideList(list, rest, scale);
    }
}

void BuildSplitLayout(QuickContext& ctx) {
    const float scale = ctx.spec.tile_size;
    const bool upright = Aspect(ctx) < 0.95f;
    const QuickAction* states = ctx.Find("states");
    // Side by side, or one above the other on an upright screen.
    ui::Node& root = *ctx.root.Add(ui::MakeBox(upright ? ui::Axis::Column : ui::Axis::Row, 18));
    root.id = "companion.split";
    root.grow = 1;

    ui::Node& list = *root.Add(ui::MakeList(8));
    list.id = "companion.page";
    list.grow = 1;
    list.width = upright ? ui::AUTO : 0;
    list.height = upright ? 0 : ui::AUTO;
    ctx.beside(list);
    AddQuickCover(list, ctx, 48, 64, true);
    std::vector<const QuickAction*> controls;
    for (const QuickAction& action : ctx.actions) {
        if (action.id != "states") {
            controls.push_back(&action);
        }
    }
    WideList(list, controls, scale);
    if (states == nullptr) {
        return;
    }
    ui::Node& slots = *root.Add(ui::MakeBox(ui::Axis::Column, GAP));
    slots.id = "companion.split_states";
    slots.grow = 1;
    slots.width = upright ? ui::AUTO : 0;
    slots.height = upright ? 0 : ui::AUTO;
    auto* title = slots.Emplace<ui::Label>("States · " + states->value, Svc().theme->text_small, gfx::FONT_BOLD);
    title->tone = ui::Tone::Dim;
    ctx.states(slots, upright ? 4 : Aspect(ctx) < 1.45f ? 2 : 3);
}

} // namespace den::app
