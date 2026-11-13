// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "app_grid.h"
#include "apps_catalog.h"
#include "den/app/app.h"

namespace den::app {

namespace {

constexpr const char* APP_MARK = ".app.";
constexpr const char* RECENT_MARK = ".recent.";

AppTile* AddTile(ui::Node* parent, const system::AppItem& app, const std::string& id, bool pinned,
                 const AppGridOptions& options) {
    auto* tile = parent->Emplace<AppTile>(app.label, app.icon);
    tile->id = id;
    tile->pinned = pinned;
    if (options.on_open) {
        tile->on_activate = [open = options.on_open, app] { open(app); };
    }
    if (options.on_hold) {
        tile->on_hold = [hold = options.on_hold, app] { hold(app); };
    }
    return tile;
}

ui::Label* AddHeading(ui::Node* parent, const std::string& text) {
    auto heading = ui::MakeHeading(text);
    heading->size = 20;
    heading->tone = ui::Tone::Dim;
    heading->pad_left = 10;
    ui::Label* raw = heading.get();
    parent->Add(std::move(heading));
    return raw;
}

} // namespace

AppTile::AppTile(std::string name, std::string icon_path) : name_(std::move(name)), icon_(std::move(icon_path)) {
    focusable = true;
    label = name_;
    width = APP_TILE_W;
    height = APP_ICON + 10 + 24;
}

gfx::Rect AppTile::FocusRect() const {
    return gfx::Rect{rect.x + (rect.w - APP_ICON) * 0.5f - 6, rect.y, APP_ICON + 12, APP_ICON + 12};
}

void AppTile::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    const gfx::Rect icon{rect.x + (rect.w - APP_ICON) * 0.5f, rect.y + 6, APP_ICON, APP_ICON};
    const gfx::Texture texture = ctx.textures && !icon_.empty() ? ctx.textures->Get(icon_, 256) : gfx::Texture{};
    if (texture) {
        canvas.DrawImage(icon, texture);
    } else {
        // No picture (yet): the first letter on a tint.
        canvas.FillRect(icon, theme.panel, theme.radius + 6);
        std::size_t end = name_.empty() ? 0 : 1;
        while (end < name_.size() && (static_cast<unsigned char>(name_[end]) & 0xC0) == 0x80) {
            ++end;
        }
        const std::string letter = name_.substr(0, end);
        const float size = 34;
        const float letter_w = ctx.Text().Measure(letter, size, gfx::FONT_BOLD);
        canvas.DrawText(letter, icon.CenterX() - letter_w * 0.5f, icon.CenterY() - size * 0.61f, size, theme.text_dim,
                        gfx::FONT_BOLD);
    }
    if (pinned) {
        // A favourite: a dot in the corner, in the text colour on the page's.
        const gfx::Rect ring{icon.Right() - 16, icon.y - 2, 18, 18};
        canvas.FillRect(ring, theme.page, 9);
        canvas.FillRect(ring.Inset(4), theme.fg, 5);
    }
    const float size = theme.text_small;
    const int font = focused ? gfx::FONT_BOLD : gfx::FONT_REGULAR;
    const std::string shown = ctx.Text().Elide(name_, size, rect.w - 4, font);
    const float shown_w = ctx.Text().Measure(shown, size, font);
    canvas.DrawText(shown, rect.CenterX() - shown_w * 0.5f, icon.Bottom() + 12, size, focused ? theme.fg : theme.text_dim,
                    font);
}

std::string AppInNodeId(const std::string& node_id) {
    for (const char* mark : {APP_MARK, RECENT_MARK}) {
        if (const auto at = node_id.find(mark); at != std::string::npos) {
            return node_id.substr(at + std::string(mark).size());
        }
    }
    return {};
}

ui::Node* AddAppGrid(ui::Node* parent, const AppGridOptions& options) {
    const system::AppShelf& shelf = Apps();
    ui::Node* list = parent->Add(ui::MakeList(12));
    list->id = options.id_prefix + ".grid";
    list->grow = 1;
    list->pad_top = 6;
    list->pad_bottom = 28;
    list->scroll_margin = 40;

    const std::vector<system::AppItem> apps = shelf.Visible(options.query);
    if (apps.empty()) {
        auto* none = list->Emplace<ui::Label>(AppsLoading()             ? "Looking for apps..."
                                              : options.query.empty() ? "No apps were found on this device"
                                                                      : "No app matches \"" + options.query + "\"");
        none->tone = ui::Tone::Dim;
        none->pad_left = 10;
        return list;
    }

    const std::vector<system::AppItem> recent = options.query.empty() ? shelf.Recent() : std::vector<system::AppItem>();
    if (!recent.empty()) {
        ui::Node* section = list->Add(ui::MakeBox(ui::Axis::Column, 4));
        AddHeading(section, "Recent")->whole = true;
        ui::Node* row = section->Add(ui::MakeShelf(APP_TILE_GAP));
        row->id = options.id_prefix + ".recent";
        row->SetPadding(10, 8);
        row->pad_right = 32;
        for (const system::AppItem& app : recent) {
            AddTile(row, app, options.id_prefix + RECENT_MARK + app.id, false, options);
        }
        AddHeading(list, "All apps")->whole = true;
    }

    const int columns = std::max(
        2, static_cast<int>((options.width - 20 + APP_TILE_GAP) / (APP_TILE_W + APP_TILE_GAP)));
    ui::Node* row = nullptr;
    int in_row = 0;
    for (const system::AppItem& app : apps) {
        if (!row || in_row == columns) {
            row = list->Add(ui::MakeBox(ui::Axis::Row, APP_TILE_GAP));
            row->align = ui::Align::Start;
            row->pad_left = 10;
            row->pad_top = 6;
            in_row = 0;
        }
        AddTile(row, app, options.id_prefix + APP_MARK + app.id, shelf.Favourite(app.id), options);
        ++in_row;
    }
    return list;
}

} // namespace den::app
