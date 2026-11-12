// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Widgets the launcher screens share: the blurred artwork behind a page, a game's cover, a
// game tile, a grid of them, a progress bar.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "den/library/library.h"
#include "den/ui/widgets.h"

namespace den::app {

constexpr float COVER_W = 168, COVER_H = 224;
constexpr float GRID_COVER_W = 144, GRID_COVER_H = 192;

/// The selected game's cover, blurred and washed towards the page colour, behind a page.
/// The blur is made once per cover on the job system from a small copy of the image.
class Backdrop {
public:
    void Set(const std::string& cover_path);
    void Draw(ui::Context& ctx, const gfx::Rect& area);

private:
    std::string current_, previous_;
    float fade_ = 1.0f;
};

/// A cover that is not focusable: the image, or the game's name on a tinted card.
class CoverView final : public ui::Node {
public:
    CoverView(std::string title, std::string image_path, float w, float h);
    bool overlay_title = false; // the image is a drawn placeholder: print the name over it

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    std::string title_, image_path_;
};

/// A bar that fills with `fraction` (0..1); a negative fraction shows a moving block.
class ProgressBar final : public ui::Node {
public:
    ProgressBar();
    float fraction = -1.0f;

protected:
    void DrawContent(ui::Context& ctx) override;
};

struct GameTileOptions {
    std::string id_prefix;  // tile id = prefix + "." + game id
    std::string badge;      // empty = the console's short name
    bool show_badge = true;
    float w = COVER_W, h = COVER_H;
    std::function<void(const library::Entry&)> on_activate;
    std::function<void(const library::Entry&)> on_focus;
};

/// A tile for a game; a placeholder cover gets the game's name printed over it.
class GameTile final : public ui::Tile {
public:
    using ui::Tile::Tile;
    bool overlay_title = false;

protected:
    void DrawContent(ui::Context& ctx) override;
};

ui::Tile* AddGameTile(ui::Node* parent, const library::Entry& game, const GameTileOptions& options);

/// How many grid tiles fit beside each other in `width`.
int GridColumns(float width, float tile_w = GRID_COVER_W, float gap = 18);
/// A vertically scrolling grid: rows of `columns` tiles. Returns the list node.
ui::Node* AddGameGrid(ui::Node* parent, const std::vector<library::Entry>& games, int columns,
                      GameTileOptions options);

} // namespace den::app
