// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The grid of the device's apps, shared by the Apps section of the main menu and the Apps
// page of the second screen: the apps opened last as a shelf, then every app as rows of
// icons with their names, favourites first.

#pragma once

#include <functional>
#include <string>

#include "den/system/app_shelf.h"
#include "den/ui/widgets.h"

namespace den::app {

constexpr float APP_TILE_W = 116, APP_ICON = 84, APP_TILE_GAP = 14;

/// An app: its icon with its name under it. Without a picture, its first letter on a tint.
class AppTile final : public ui::Node {
public:
    AppTile(std::string name, std::string icon_path);
    bool pinned = false; // a favourite: marked in the icon's corner

    gfx::Rect FocusRect() const override;

protected:
    void DrawContent(ui::Context& ctx) override;

private:
    std::string name_, icon_;
};

/// A button for one app: its icon, then a text ("Back to YouTube", or just its name).
class AppChip final : public ui::Node {
public:
    AppChip(std::string text, std::string icon_path);
    bool strong = false; // the row's main action: the stronger tint and bold text

protected:
    ui::Size MeasureContent(ui::Context& ctx) override;
    void DrawContent(ui::Context& ctx) override;

private:
    std::string text_, icon_;
};

struct AppGridOptions {
    std::string id_prefix;  // ids are prefix + ".app." + app id and prefix + ".recent." + app id
    float width = 0;        // the room the rows have, in units
    std::string query;      // only the apps matching it; no recent shelf then
    std::function<void(const system::AppItem&)> on_open;
    std::function<void(const system::AppItem&)> on_hold; // a finger held on an app
};

/// Adds the scrolling list to `parent` (it takes the free room) and returns it.
ui::Node* AddAppGrid(ui::Node* parent, const AppGridOptions& options);
/// The app a tile's id stands for; empty for other nodes.
std::string AppInNodeId(const std::string& node_id);

} // namespace den::app
