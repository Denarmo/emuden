// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Library entry of the rail: every game as a grid, filtered by console with the chips
// above it (or the shoulder buttons). Route "library", optional param system.

#include <algorithm>

#include "../details/launch.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "game_widgets.h"
#include "games.h"
#include "strings.h"

namespace den::app {

namespace {

constexpr float RAIL_W = 232;

class LibraryScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = GamesRevision();
        width_ = CanvasWidth();
        if (!filter_set_) {
            filter_ = Param("system");
            filter_set_ = true;
        }
        std::vector<library::Entry> games = WithoutHidden(svc.library->Entries());

        // The consoles that have games, in the plugins' order.
        systems_.clear();
        for (const plugins::Console& console : svc.plugins->Consoles()) {
            if (HasSystem(games, console.id)) {
                systems_.push_back(console.id);
            }
        }
        for (const library::Entry& game : games) {
            if (std::find(systems_.begin(), systems_.end(), game.system) == systems_.end()) {
                systems_.push_back(game.system);
            }
        }
        if (!filter_.empty() && std::find(systems_.begin(), systems_.end(), filter_) == systems_.end()) {
            filter_.clear();
        }
        if (!filter_.empty()) {
            std::erase_if(games, [&](const library::Entry& game) { return game.system != filter_; });
        }

        ui::Node* content = BuildRailFrame(*this, "");
        content->gap = 10;
        ui::Node* header = content->Add(ui::MakeBox(ui::Axis::Row, 16));
        header->align = ui::Align::End;
        header->Emplace<ui::Label>(Tr("library.title"), svc.theme->text_title, gfx::FONT_BOLD);
        auto* count = header->Emplace<ui::Label>(TrF("library.count", games.size()));
        count->tone = ui::Tone::Dim;
        count->pad_bottom = 6;

        ui::Node* chips = content->Add(ui::MakeShelf(8));
        chips->id = "library.chips";
        chips->SetPadding(8, 6);
        AddChip(chips, "", Tr("library.all"));
        for (const std::string& system : systems_) {
            AddChip(chips, system, SystemName(system));
        }

        if (games.empty()) {
            content->Emplace<ui::Label>(Tr("home.empty_message"))->tone = ui::Tone::Dim;
            return;
        }
        GameTileOptions options;
        options.id_prefix = "library";
        options.show_badge = filter_.empty();
        options.on_activate = [](const library::Entry& game) {
            Svc().stage->Push("details", {{"id", game.id}});
        };
        options.on_focus = [this](const library::Entry& game) { selected_ = game.id; };
        const float room = width_ - RAIL_W - svc.theme->margin * 2 - 10;
        ui::Node* grid = AddGameGrid(content, games, GridColumns(room), options);
        grid->id = "library.grid";
    }

    void Update(ui::Context&) override {
        if (GamesRevision() != revision_ || CanvasWidth() != width_) {
            Rebuild();
            return;
        }
        if (focus_grid_) {
            focus_grid_ = false;
            std::vector<ui::Node*> tiles;
            if (ui::Node* grid = Root().FindById("library.grid")) {
                grid->CollectFocusable(tiles);
            }
            if (!tiles.empty()) {
                SetFocus(tiles.front());
            }
        }
    }

    bool OnNav(input::Nav nav) override {
        switch (nav) {
        case input::Nav::ShoulderL:
        case input::Nav::ShoulderR:
            StepFilter(nav == input::Nav::ShoulderR ? 1 : -1);
            return true;
        case input::Nav::Option:
            if (OnGame()) {
                StartGame(selected_);
            }
            return true;
        case input::Nav::Extra:
            if (const auto game = OnGame() ? Svc().library->Find(selected_) : std::nullopt) {
                Svc().library->SetFavourite(game->id, !game->favourite);
                Svc().stage->Toast(TrF(game->favourite ? "home.unfavourited" : "home.favourited", game->name),
                                   2.0f);
            }
            return true;
        case input::Nav::Back:
            if (Svc().stage->Depth() > 1) {
                Svc().stage->Pop();
            } else {
                Svc().stage->Replace("home");
            }
            return true;
        default:
            return false;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        std::vector<ui::Hint> hints{{input::Nav::Confirm, OnGame() ? Tr("hint.details") : Tr("hint.select")}};
        if (OnGame()) {
            hints.push_back({input::Nav::Option, Tr("hint.start")});
            hints.push_back({input::Nav::Extra, Tr("hint.favourite")});
        }
        hints.push_back({input::Nav::ShoulderR, Tr("hint.next_console")});
        hints.push_back({input::Nav::Back, Tr("common.back")});
        return hints;
    }

private:
    static float CanvasWidth() {
        const float width = Svc().canvas->Width();
        return width > 0 ? width : 1280.0f;
    }

    static bool HasSystem(const std::vector<library::Entry>& games, const std::string& system) {
        return std::any_of(games.begin(), games.end(),
                           [&](const library::Entry& game) { return game.system == system; });
    }

    bool OnGame() const {
        return !selected_.empty() && Focus() && Focus()->id == "library." + selected_;
    }

    void AddChip(ui::Node* chips, const std::string& system, const std::string& text) {
        auto* chip = chips->Emplace<ui::Button>(text, [this, system] { SetFilter(system); });
        chip->id = "library.chip." + system;
        chip->height = 40;
        chip->selected = system == filter_;
    }

    void SetFilter(const std::string& system) {
        if (system != filter_) {
            filter_ = system;
            selected_.clear();
            Rebuild();
        }
    }

    void StepFilter(int direction) {
        std::vector<std::string> all{""};
        all.insert(all.end(), systems_.begin(), systems_.end());
        const auto it = std::find(all.begin(), all.end(), filter_);
        const int count = static_cast<int>(all.size());
        const int index = it == all.end() ? 0 : static_cast<int>(it - all.begin());
        SetFilter(all[static_cast<std::size_t>(((index + direction) % count + count) % count)]);
        focus_grid_ = true;
    }

    std::vector<std::string> systems_;
    std::string filter_, selected_;
    bool filter_set_ = false, focus_grid_ = false;
    float width_ = 0;
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("library", LibraryScreen);
DEN_REGISTER_RAIL(10, "Library", "library");

} // namespace den::app
